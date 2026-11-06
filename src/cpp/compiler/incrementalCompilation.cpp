#include "incrementalCompilation.h"
#include "codegen/codegen.h"
#include "codegen/native.h"
#include "compiler.h"
#include "incrementalMatcherCache.h"
#include "lsp/fileSystem.h"
#include "pathUtils.h"
#include "syntaxConfig.h"
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
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <stdexcept>

namespace {
using Json = nlohmann::json;
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

std::string semanticSourceDigest(std::string_view content, std::string_view commentPrefix) {
	std::string semantic;
	semantic.reserve(content.size());
	for (size_t start = 0; start < content.size();) {
		size_t end = content.find('\n', start);
		if (end == std::string_view::npos)
			end = content.size();
		std::string_view line = content.substr(start, end - start);
		if (size_t comment = findCommentStart(line, commentPrefix); comment != std::string_view::npos)
			line = line.substr(0, comment);
		while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back())))
			line.remove_suffix(1);
		if (!line.empty()) {
			semantic.append(line);
			semantic.push_back('\n');
		}
		start = end + (end < content.size());
	}
	return digest(semantic);
}

class RecordingFileSystem : public lsp::FileSystem {
  public:
	lsp::SourceFile *getFile(const std::string &path) override {
		lsp::SourceFile *file = files.getFile(path);
		observations[path] = observeFile(path, file);
		return file;
	}
	lsp::SourceFile *cachedFile(const std::string &path) { return files.getFile(path); }
	Json observations = Json::object();

  private:
	lsp::LocalFileSystem files;
};

void recordSemanticSources(Json &result, RecordingFileSystem &files, const ParseContext &context) {
	result["comment_prefixes"] =
		Json{{"builtin", context.builtinSyntax.commentPrefix}, {"project", context.projectSyntax.commentPrefix}};
	result["config_paths"] = Json::array({context.builtinSyntaxConfigPath, context.projectSyntaxConfigPath});
	for (auto &[path, observation] : result["files"].items()) {
		if (observation.is_null() || fs::path(path).extension() != ".dl" || path == context.builtinSyntaxConfigPath ||
			path == context.projectSyntaxConfigPath)
			continue;
		lsp::SourceFile *file = files.cachedFile(path);
		if (!file)
			throw std::runtime_error("recorded source disappeared: " + path);
		const std::string &prefix =
			file->isInternal ? context.builtinSyntax.commentPrefix : context.projectSyntax.commentPrefix;
		observation["semantic"] = semanticSourceDigest(file->content, prefix);
	}
}

bool observationsStillEquivalent(const Json &previous, RecordingFileSystem &files, const ParseContext::Options &options) {
	std::vector<std::string> changedSources;
	for (const auto &[path, observation] : previous.at("files").items()) {
		Json current = observeFile(path, files.getFile(path));
		Json rawObservation = observation;
		if (rawObservation.is_object())
			rawObservation.erase("semantic");
		if (current == rawObservation)
			continue;
		if (current.is_null() || observation.is_null() || fs::path(path).extension() != ".dl" ||
			!previous.contains("comment_prefixes") || !observation.contains("semantic"))
			return false;
		if (current.at("canonical") != observation.at("canonical") || current.at("internal") != observation.at("internal"))
			return false;
		changedSources.push_back(path);
	}
	if (changedSources.empty())
		return true;
	if (options.emitDebugInfo || options.traceExecution)
		return false;
	const Json &configPaths = previous.at("config_paths");
	const Json &prefixes = previous.at("comment_prefixes");
	std::set<std::string> diagnosticSources = previous.at("diagnostic_sources").get<std::set<std::string>>();
	for (const std::string &path : changedSources) {
		if (path == configPaths[0].get<std::string>() || path == configPaths[1].get<std::string>())
			return false;
		if (diagnosticSources.contains(path))
			return false;
		lsp::SourceFile *file = files.cachedFile(path);
		if (!file)
			throw std::runtime_error("recorded source disappeared: " + path);
		const std::string &prefix = file->isInternal ? prefixes.at("builtin").get_ref<const std::string &>()
													 : prefixes.at("project").get_ref<const std::string &>();
		if (semanticSourceDigest(file->content, prefix) != previous.at("files").at(path).at("semantic").get<std::string>())
			return false;
	}
	return true;
}

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
		size_t prefixLength = name.starts_with("object-")	 ? 7
							  : name.starts_with("program-") ? 8
							  : name.starts_with("matcher-") ? 8
															 : 0;
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
	bool linkedCachedObjects = false;
	size_t reusedObjects = 0;
	size_t builtObjects = 0;
	size_t matcherCacheHits = 0;
	size_t precedenceMatcherCacheHits = 0;
	std::vector<std::string> objectKeys;
};

std::optional<bool> linkCachedObjects(
	ParseContext &context, const fs::path &directory, const std::vector<std::string> &keys,
	const std::vector<std::string> &libraries, BuildStatistics &statistics
) {
	TemporaryObjects temporary;
	std::vector<std::string> paths;
	paths.reserve(keys.size());
	for (size_t index = 0; index < keys.size(); index++) {
		std::optional<std::string> object = readEntry(directory / ("object-" + keys[index] + ".entry"));
		if (!object)
			return std::nullopt;
		fs::path path = temporary.directory / (std::to_string(index) + ".o");
		std::ofstream output(path, std::ios::binary);
		output.write(object->data(), object->size());
		output.close();
		if (!output)
			throw std::runtime_error("cannot materialize cached object: " + path.string());
		paths.push_back(path.string());
	}
	statistics.reusedObjects = keys.size();
	statistics.linkedCachedObjects = true;
	return emitNativeExecutableWithObjects(context, paths, libraries);
}

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
		statistics.objectKeys.push_back(key);
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
		std::string previousMatches;
		std::string previousMatcherKey;
		if (auto cached = readEntry(programEntry)) {
			previous = Json::parse(*cached);
			if (!observationsStillEquivalent(*previous, *recording, context.options)) {
				previousMatcherKey = previous->at("matcher_key").get<std::string>();
				previous.reset();
			}
		}
		if (previous) {
			statistics.reusedFrontend = true;
			if (!initializeSyntaxConfigs(context, context.options.inputPath))
				return false;
			if (!previous->contains("comment_prefixes")) {
				recordSemanticSources(*previous, *recording, context);
				writeEntry(programEntry, previous->dump());
			}
			std::vector<std::string> objectKeys = previous->at("object_keys").get<std::vector<std::string>>();
			context.llvmContext = new llvm::LLVMContext();
			context.llvmModule = new llvm::Module("dynlex_cached_link", *context.llvmContext);
			std::string error;
			context.targetMachine = createNativeTargetMachine(context, error);
			if (!context.targetMachine)
				throw std::runtime_error(error);
			std::vector<std::string> libraries = previous->at("libraries").get<std::vector<std::string>>();
			context.requiredLibraries.insert(libraries.begin(), libraries.end());
			std::cerr << previous->at("diagnostics").get<std::string>();
			std::optional<bool> cachedLink = linkCachedObjects(context, directory, objectKeys, libraries, statistics);
			if (cachedLink) {
				success = *cachedLink;
			} else {
				delete context.llvmModule;
				context.llvmModule = nullptr;
				llvm::SMDiagnostic diagnostic;
				std::unique_ptr<llvm::Module> module =
					llvm::parseAssemblyString(previous->at("ir").get<std::string>(), diagnostic, *context.llvmContext);
				if (!module || llvm::verifyModule(*module))
					throw std::runtime_error("invalid cached LLVM IR: " + programEntry.string());
				module->setModuleIdentifier(previous->at("module_id").get<std::string>());
				context.llvmModule = module.release();
				success = emitPartitions(context, directory, compilerIdentity, options, libraries, statistics);
				if (success) {
					(*previous)["object_keys"] = statistics.objectKeys;
					writeEntry(programEntry, previous->dump());
				}
			}
		} else {
			recording->observations = Json::object();
			if (!previousMatcherKey.empty()) {
				fs::path matcherEntry = directory / ("matcher-" + previousMatcherKey + ".entry");
				if (auto cached = readEntry(matcherEntry)) {
					if (digest(*cached) != previousMatcherKey)
						throw std::runtime_error("corrupt matcher cache entry: " + matcherEntry.string());
					previousMatches = std::move(*cached);
				}
			}
			IncrementalMatcherCache matcherCache(context, previousMatches);
			context.reuseMatcherResult = [&](PatternReference *reference, MatchOptions options,
											 MatchDependencies *dependencies) {
				return matcherCache.reuse(reference, options, dependencies);
			};
			context.recordMatcherResult = [&](PatternReference *reference, MatchOptions options, const PatternMatch *match,
											  const MatchDependencies *dependencies) {
				matcherCache.record(reference, options, match, dependencies);
			};
			bool compiled = compile(context.options.inputPath, context);
			statistics.matcherCacheHits = matcherCache.hits();
			statistics.precedenceMatcherCacheHits = matcherCache.precedenceHits();
			context.reuseMatcherResult = {};
			context.recordMatcherResult = {};
			if (compiled) {
				success = generateCode(context, [&](ParseContext &compiled) {
					std::vector<std::string> libraries(compiled.requiredLibraries.begin(), compiled.requiredLibraries.end());
					std::string diagnostics;
					std::set<std::string> diagnosticSources;
					auto recordDiagnosticSource = [&](const Range &range) {
						SourceLocation start = range.sourceStart();
						SourceLocation end = range.sourceEnd();
						if (start.sourceFile)
							diagnosticSources.insert(start.sourceFile->uri);
						if (end.sourceFile)
							diagnosticSources.insert(end.sourceFile->uri);
					};
					for (const Diagnostic &diagnostic : compiled.diagnostics) {
						diagnostics += diagnostic.toString() + "\n";
						recordDiagnosticSource(diagnostic.range);
						for (const RelatedInfo &related : diagnostic.relatedInfo)
							recordDiagnosticSource(related.range);
					}
					Json result{
						{"files", recording->observations},
						{"ir", renderModule(*compiled.llvmModule)},
						{"module_id", compiled.llvmModule->getModuleIdentifier()},
						{"libraries", libraries},
						{"diagnostics", diagnostics},
						{"diagnostic_sources", diagnosticSources}
					};
					std::string matcherPayload = matcherCache.snapshot();
					std::string matcherKey = digest(matcherPayload);
					writeEntry(directory / ("matcher-" + matcherKey + ".entry"), matcherPayload);
					result["matcher_key"] = matcherKey;
					recordSemanticSources(result, *recording, compiled);
					if (!emitPartitions(compiled, directory, compilerIdentity, options, libraries, statistics))
						return false;
					result["object_keys"] = statistics.objectKeys;
					writeEntry(programEntry, result.dump());
					return true;
				});
			}
		}
		trimCache(directory);
		if (showStatistics)
			std::cerr << "incremental: "
					  << Json{{"frontend_reused", statistics.reusedFrontend},
							  {"linked_cached_objects", statistics.linkedCachedObjects},
							  {"objects_reused", statistics.reusedObjects},
							  {"objects_built", statistics.builtObjects},
							  {"matcher_cache_hits", statistics.matcherCacheHits},
							  {"precedence_matcher_cache_hits", statistics.precedenceMatcherCacheHits}}
							 .dump()
					  << '\n';
		return success;
	} catch (const std::exception &error) {
		std::cerr << "Incremental compilation failed: " << error.what() << '\n';
		return false;
	}
}
