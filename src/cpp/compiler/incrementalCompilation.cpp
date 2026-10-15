#include "incrementalCompilation.h"
#include "codegen/codegen.h"
#include "codegen/native.h"
#include "compiler.h"
#include "lsp/fileSystem.h"
#include "pathUtils.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Object/BuildID.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/SHA256.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/Transforms/IPO/StripDeadPrototypes.h"
#include "llvm/Transforms/Utils/SplitModule.h"
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>

namespace {
using Json = nlohmann::ordered_json;
namespace fs = std::filesystem;
constexpr std::string_view cacheHeader = "DynLex incremental cache v1\n";
constexpr uintmax_t maximumEntryBytes = 256 * 1024 * 1024;
constexpr uintmax_t maximumCacheBytes = 512 * 1024 * 1024;
constexpr unsigned partitionCount = 32;

std::string digest(std::string_view text) {
	llvm::SHA256 hash;
	hash.update(llvm::StringRef(text.data(), text.size()));
	return llvm::toHex(hash.final(), true);
}

std::string digestFile(const fs::path &path) {
	std::ifstream input(path, std::ios::binary);
	if (!input)
		throw std::runtime_error("cannot read " + path.string());
	llvm::SHA256 hash;
	std::array<char, 65536> buffer;
	while (input.read(buffer.data(), buffer.size()) || input.gcount())
		hash.update(llvm::StringRef(buffer.data(), input.gcount()));
	if (!input.eof())
		throw std::runtime_error("failed reading " + path.string());
	return llvm::toHex(hash.final(), true);
}

// Linker build IDs identify the complete linked compiler without hashing its
// debug sections on every invocation. Formats without a build ID use the
// exact executable content digest instead.
std::string compilerBuildIdentity(const std::string &argv0) {
	std::string executable = llvm::sys::fs::getMainExecutable(argv0.c_str(), reinterpret_cast<void *>(&compileIncrementally));
	if (executable.empty())
		throw std::runtime_error("cannot identify compiler executable");
	auto object = llvm::object::ObjectFile::createObjectFile(executable);
	if (!object)
		throw std::runtime_error("cannot inspect compiler executable: " + llvm::toString(object.takeError()));
	llvm::object::BuildIDRef buildId = llvm::object::getBuildID(object->getBinary());
	if (!buildId.empty())
		return "build-id:" + llvm::toHex(buildId, true);
	return "sha256:" + digestFile(executable);
}

std::string readFile(const fs::path &path) {
	uintmax_t size = fs::file_size(path);
	if (size > maximumEntryBytes)
		throw std::runtime_error("cache entry exceeds the size limit: " + path.string());
	std::ifstream input(path, std::ios::binary);
	if (!input)
		throw std::runtime_error("cannot read " + path.string());
	std::string result(size, '\0');
	input.read(result.data(), result.size());
	if (static_cast<size_t>(input.gcount()) != result.size())
		throw std::runtime_error("incomplete read: " + path.string());
	return result;
}

std::optional<std::string> readEntry(const fs::path &path) {
	std::error_code error;
	if (!fs::exists(path, error)) {
		if (error)
			throw fs::filesystem_error("cannot inspect cache entry", path, error);
		return std::nullopt;
	}
	std::string entry;
	try {
		entry = readFile(path);
	} catch (const fs::filesystem_error &exception) {
		if (exception.code() == std::errc::no_such_file_or_directory)
			return std::nullopt; // Another build may have evicted this entry.
		throw;
	}
	const size_t payloadOffset = cacheHeader.size() + 65;
	if (entry.size() < payloadOffset || !entry.starts_with(cacheHeader) || entry[payloadOffset - 1] != '\n' ||
		digest(std::string_view(entry).substr(payloadOffset)) != entry.substr(cacheHeader.size(), 64))
		throw std::runtime_error("corrupt cache entry: " + path.string());
	return entry.substr(payloadOffset);
}

void writeEntry(const fs::path &path, std::string_view payload) {
	if (payload.size() + cacheHeader.size() + 65 > maximumEntryBytes)
		throw std::runtime_error("cache entry exceeds the size limit: " + path.string());
	llvm::SmallString<256> temporary;
	int descriptor;
	if (std::error_code error = llvm::sys::fs::createUniqueFile(path.string() + ".tmp-%%%%%%", descriptor, temporary))
		throw fs::filesystem_error("cannot create cache entry", path, error);
	llvm::raw_fd_ostream output(descriptor, true);
	output << cacheHeader << digest(payload) << '\n' << payload;
	output.close();
	if (output.has_error()) {
		std::error_code error = output.error();
		output.clear_error();
		llvm::sys::fs::remove(temporary);
		throw fs::filesystem_error("cannot write cache entry", path, error);
	}
	if (std::error_code error = llvm::sys::fs::rename(temporary, path.string())) {
		llvm::sys::fs::remove(temporary);
		throw fs::filesystem_error("cannot publish cache entry", path, error);
	}
}

Json observeFile(const std::string &path, lsp::SourceFile *file) {
	if (!file)
		return nullptr;
	return Json{
		{"content", digest(file->content)},
		{"canonical", fs::canonical(pathutil::toFilesystemPath(path)).string()},
		{"internal", file->isInternal}
	};
}

class RecordingFileSystem : public lsp::FileSystem {
  public:
	lsp::SourceFile *getFile(const std::string &path) override {
		lsp::SourceFile *file = files.getFile(path);
		observations[path] = observeFile(path, file);
		return file;
	}
	Json observations = Json::object();

  private:
	lsp::LocalFileSystem files;
};

Json optionIdentity(const ParseContext::Options &options) {
	auto optionalBool = [](std::optional<bool> value) -> Json {
		return value ? Json(*value) : Json(nullptr);
	};
	llvm::StringMap<bool> hostFeatures = llvm::sys::getHostCPUFeatures();
	std::map<std::string, bool> sortedFeatures;
	for (const auto &feature : hostFeatures)
		sortedFeatures[feature.first().str()] = feature.second;
	return Json{
		{"input", options.inputPath},
		{"cwd", fs::current_path().string()},
		{"no_main", options.noMain},
		{"optimization", options.optimizationLevel},
		{"size", static_cast<int>(options.optimizationSize)},
		{"fast_math", options.fastMath},
		{"contract", static_cast<int>(options.floatingPointContract)},
		{"vectorize", optionalBool(options.loopVectorization)},
		{"slp", optionalBool(options.slpVectorization)},
		{"unroll", optionalBool(options.loopUnrolling)},
		{"cpu", options.targetCPU},
		{"tune", options.targetTuneCPU},
		{"features", options.targetFeatures},
		{"explicit_target", options.hasExplicitTargetConfiguration},
		{"debug", options.emitDebugInfo},
		{"trace", options.traceExecution},
		{"resolution_limit", options.maxResolutionIterations},
		{"host_cpu", llvm::sys::getHostCPUName().str()},
		{"host_features", sortedFeatures}
	};
}

std::string renderModule(const llvm::Module &module) {
	std::string result;
	llvm::raw_string_ostream output(result);
	module.print(output, nullptr);
	return result;
}

void trimCache(const fs::path &directory) {
	struct Entry {
		fs::path path;
		uintmax_t size;
		fs::file_time_type time;
	};
	std::vector<Entry> entries;
	uintmax_t size = 0;
	for (const auto &file : fs::directory_iterator(directory)) {
		if (file.path().extension() != ".entry")
			continue;
		std::string name = file.path().stem().string();
		size_t prefixLength = name.starts_with("object-") ? 7 : name.starts_with("program-") ? 8 : 0;
		if (!prefixLength || name.size() != prefixLength + 64 ||
			!std::all_of(name.begin() + prefixLength, name.end(), [](char character) {
			return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f');
		}))
			continue;
		std::error_code error;
		uintmax_t entrySize = file.file_size(error);
		if (error == std::errc::no_such_file_or_directory)
			continue;
		if (error)
			throw fs::filesystem_error("cannot inspect cache size", file.path(), error);
		auto time = file.last_write_time(error);
		if (error == std::errc::no_such_file_or_directory)
			continue;
		if (error)
			throw fs::filesystem_error("cannot inspect cache age", file.path(), error);
		entries.push_back({file.path(), entrySize, time});
		size += entrySize;
	}
	std::sort(entries.begin(), entries.end(), [](const Entry &left, const Entry &right) {
		return left.time == right.time ? left.path < right.path : left.time < right.time;
	});
	for (const Entry &entry : entries) {
		if (size <= maximumCacheBytes)
			break;
		fs::remove(entry.path);
		size -= entry.size;
	}
}

struct TemporaryObjects {
	fs::path directory;
	TemporaryObjects() {
		llvm::SmallString<256> path;
		if (std::error_code error = llvm::sys::fs::createUniqueDirectory("dynlex-incremental", path))
			throw fs::filesystem_error("cannot create incremental build directory", error);
		directory = path.str().str();
	}
	~TemporaryObjects() {
		if (directory.empty())
			return;
		std::error_code error;
		fs::remove_all(directory, error);
		if (error)
			std::cerr << "Failed to clean incremental build directory: " << error.message() << '\n';
	}
};

struct BuildStatistics {
	bool reusedFrontend = false;
	size_t reusedObjects = 0;
	size_t builtObjects = 0;
};

bool emitPartitions(
	ParseContext &context, const fs::path &directory, const std::string &compilerIdentity, const Json &options,
	const std::vector<std::string> &libraries, BuildStatistics &statistics
) {
	TemporaryObjects temporary;
	std::vector<std::string> objects;
	std::string prefix = "__dynlex_" + digest(fs::absolute(context.options.inputPath).lexically_normal().string()) + "_";
	for (llvm::GlobalValue &value : context.llvmModule->global_values()) {
		if (value.hasLocalLinkage())
			value.setName(prefix + value.getName().str());
	}
	bool success = true;
	llvm::SplitModule(*context.llvmModule, partitionCount, [&](std::unique_ptr<llvm::Module> part) {
		if (!success)
			return;
		bool hasDefinition =
			std::any_of(part->global_values().begin(), part->global_values().end(), [](const llvm::GlobalValue &value) {
			return !value.isDeclaration();
		});
		if (!hasDefinition && part->getModuleInlineAsm().empty())
			return;
		llvm::ModuleAnalysisManager analyses;
		llvm::StripDeadPrototypesPass().run(*part, analyses);
		std::string ir = renderModule(*part);
		std::string key = digest(compilerIdentity + options.dump() + ir);
		fs::path entry = directory / ("object-" + key + ".entry");
		fs::path object = temporary.directory / (std::to_string(objects.size()) + ".o");
		if (std::optional<std::string> cached = readEntry(entry)) {
			std::ofstream output(object, std::ios::binary);
			output.write(cached->data(), cached->size());
			output.close();
			if (!output)
				throw std::runtime_error("cannot materialize cached object: " + object.string());
			statistics.reusedObjects++;
		} else {
			if (!emitNativeObjectModule(context, *part, object.string())) {
				success = false;
				return;
			}
			writeEntry(entry, readFile(object));
			statistics.builtObjects++;
		}
		objects.push_back(object.string());
	});
	bool linked = success && emitNativeExecutableWithObjects(context, objects, libraries);
	fs::remove_all(temporary.directory);
	temporary.directory.clear();
	return linked;
}
} // namespace

bool compileIncrementally(
	ParseContext &context, const std::string &cacheDirectory, const std::string &compilerPath, bool showStatistics
) {
	try {
		fs::path directory = fs::absolute(cacheDirectory) / "native-v1";
		fs::create_directories(directory);
		std::string compilerIdentity = compilerBuildIdentity(compilerPath);
		Json options = optionIdentity(context.options);
		fs::path programEntry = directory / ("program-" + digest(compilerIdentity + options.dump()) + ".entry");
		auto files = std::make_unique<RecordingFileSystem>();
		RecordingFileSystem *recording = files.get();
		context.fileSystem = std::move(files);
		BuildStatistics statistics;
		bool success = false;
		std::optional<Json> previous;
		if (auto cached = readEntry(programEntry)) {
			previous = Json::parse(*cached);
			bool unchanged = true;
			for (const auto &[path, observation] : previous->at("files").items()) {
				if (observeFile(path, recording->getFile(path)) != observation) {
					unchanged = false;
					break;
				}
			}
			if (!unchanged)
				previous.reset();
		}
		if (previous) {
			statistics.reusedFrontend = true;
			if (!initializeSyntaxConfigs(context, context.options.inputPath))
				return false;
			context.llvmContext = new llvm::LLVMContext();
			llvm::SMDiagnostic diagnostic;
			std::unique_ptr<llvm::Module> module =
				llvm::parseAssemblyString(previous->at("ir").get<std::string>(), diagnostic, *context.llvmContext);
			if (!module || llvm::verifyModule(*module))
				throw std::runtime_error("invalid cached LLVM IR: " + programEntry.string());
			module->setModuleIdentifier(previous->at("module_id").get<std::string>());
			context.llvmModule = module.release();
			std::string error;
			context.targetMachine = createNativeTargetMachine(context, error);
			if (!context.targetMachine)
				throw std::runtime_error(error);
			std::vector<std::string> libraries = previous->at("libraries").get<std::vector<std::string>>();
			context.requiredLibraries.insert(libraries.begin(), libraries.end());
			std::cerr << previous->at("diagnostics").get<std::string>();
			success = emitPartitions(context, directory, compilerIdentity, options, libraries, statistics);
		} else {
			recording->observations = Json::object();
			if (compile(context.options.inputPath, context)) {
				success = generateCode(context, [&](ParseContext &compiled) {
					std::vector<std::string> libraries(compiled.requiredLibraries.begin(), compiled.requiredLibraries.end());
					std::string diagnostics;
					for (const Diagnostic &diagnostic : compiled.diagnostics)
						diagnostics += diagnostic.toString() + "\n";
					Json result{
						{"files", recording->observations},
						{"ir", renderModule(*compiled.llvmModule)},
						{"module_id", compiled.llvmModule->getModuleIdentifier()},
						{"libraries", libraries},
						{"diagnostics", diagnostics}
					};
					writeEntry(programEntry, result.dump());
					return emitPartitions(compiled, directory, compilerIdentity, options, libraries, statistics);
				});
			}
		}
		trimCache(directory);
		if (showStatistics)
			std::cerr << "incremental: " << Json{{"frontend_reused", statistics.reusedFrontend},
				{"objects_reused", statistics.reusedObjects}, {"objects_built", statistics.builtObjects}}.dump() << '\n';
		return success;
	} catch (const std::exception &error) {
		std::cerr << "Incremental compilation failed: " << error.what() << '\n';
		return false;
	}
}
