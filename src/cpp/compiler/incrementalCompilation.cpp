#include "incrementalCompilation.h"
#include "codegen/codegen.h"
#include "codegen/native.h"
#include "compiler.h"
#include "expression.h"
#include "incrementalIRReuse.h"
#include "incrementalMatcherCache.h"
#include "lsp/fileSystem.h"
#include "lsp/sourceFile.h"
#include "pathUtils.h"
#include "section.h"
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
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <stdexcept>
#include <unordered_set>

namespace {
using Json = nlohmann::json;
namespace fs = std::filesystem;
constexpr std::string_view cacheHeader = "DynLex incremental cache v1\n";
constexpr uintmax_t maximumEntryBytes = 256 * 1024 * 1024;
constexpr uintmax_t maximumCacheBytes = 512 * 1024 * 1024;
constexpr unsigned partitionCount = 4;
using Clock = std::chrono::steady_clock;

double elapsedMilliseconds(Clock::time_point start) {
	return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

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

void collectSections(Section *section, const std::string &path, std::map<std::string, Section *> &sections) {
	sections.emplace(path, section);
	for (size_t index = 0; index < section->children.size(); index++)
		collectSections(section->children[index], path + "/" + std::to_string(index), sections);
}

Json sectionIdentity(const Section &section) {
	const CodeLine *opening = section.openingLine;
	if (!opening && !section.patternDefinitions.empty())
		opening = section.patternDefinitions.front()->range.line;
	requireCompilerInvariant(opening && opening->sourceFile, "incremental section has no source identity");
	return Json{
		{"uri", opening->sourceFile->uri},
		{"header",
		 section.openingLine ? std::string(opening->rightTrimmedText) : section.patternDefinitions.front()->toString()},
		{"type", static_cast<int>(section.type)}
	};
}

bool containsNestedDefinition(const Section &section) {
	for (const Section *child : section.children) {
		if (!child->patternDefinitions.empty() || containsNestedDefinition(*child))
			return true;
	}
	return false;
}

Json inactiveFunctionSections(const std::map<std::string, Section *> &sections) {
	Json inactive = Json::object();
	for (const auto &[path, section] : sections) {
		if (section->type != SectionType::Function || section->patternDefinitions.empty() || section->isFlex ||
			section->isConversion || section->isExposed || !section->instantiations.empty() ||
			containsNestedDefinition(*section))
			continue;
		inactive[path] = sectionIdentity(*section);
	}
	return inactive;
}

std::string declarationDigest(const std::map<std::string, Section *> &sections) {
	Json declarations = Json::array();
	for (const auto &[path, section] : sections) {
		if (section->patternDefinitions.empty())
			continue;
		Json definitions = Json::array();
		for (const PatternDefinition *definition : section->patternDefinitions)
			definitions.push_back(definition->toString());
		declarations.push_back(
			Json{
				{"path", path},
				{"identity", sectionIdentity(*section)},
				{"definitions", definitions},
				{"local", section->isLocal},
				{"flex", section->isFlex},
				{"exposed", section->isExposed},
				{"conversion", section->isConversion},
				{"implicit_conversion", section->isImplicitConversion},
				{"return_contract", static_cast<int>(section->returnContract)},
				{"before", section->beforePatterns},
				{"after", section->afterPatterns},
				{"associativity", static_cast<int>(section->associativity)}
			}
		);
	}
	return digest(declarations.dump());
}

Json sourceProjection(const ParseContext &context, const std::unordered_set<const Section *> &inactive) {
	std::map<const lsp::SourceFile *, std::set<int>> masked;
	std::map<const lsp::SourceFile *, std::set<int>> preserved;
	for (const CodeLine *line : context.codeLines) {
		bool insideInactive = false;
		for (const Section *section = line->section; section; section = section->parent) {
			if (inactive.contains(section)) {
				insideInactive = true;
				break;
			}
		}
		(insideInactive ? masked : preserved)[line->sourceFile].insert(line->sourceFileLineIndex);
	}
	Json sources = Json::object();
	for (const auto &[_, source] : context.importedFiles) {
		std::string projection;
		std::string_view content = source->content;
		size_t start = 0;
		int lineIndex = 0;
		bool inMaskedRun = false;
		while (start < content.size()) {
			size_t end = content.find('\n', start);
			if (end == std::string_view::npos)
				end = content.size();
			else
				end++;
			bool omit = masked[source].contains(lineIndex) && !preserved[source].contains(lineIndex);
			if (omit) {
				if (!inMaskedRun)
					projection += 'M';
			} else {
				projection += 'L';
				projection += std::to_string(end - start);
				projection += ':';
				projection.append(content.data() + start, end - start);
			}
			inMaskedRun = omit;
			start = end;
			lineIndex++;
		}
		sources[source->uri] = digest(projection);
	}
	return sources;
}

Json captureInactiveProjection(
	const ParseContext &context, const Json *priorInactive = nullptr,
	const std::set<std::string> *generatedFunctionPaths = nullptr
) {
	std::map<std::string, Section *> sections;
	collectSections(context.mainSection, "root", sections);
	Json inactive = priorInactive ? *priorInactive : inactiveFunctionSections(sections);
	if (generatedFunctionPaths) {
		for (const std::string &path : *generatedFunctionPaths)
			inactive.erase(path);
	}
	std::unordered_set<const Section *> selected;
	for (const auto &[path, identity] : inactive.items()) {
		auto section = sections.find(path);
		if (section == sections.end() || sectionIdentity(*section->second) != identity)
			return nullptr;
		selected.insert(section->second);
	}
	return Json{
		{"inactive", inactive}, {"declarations", declarationDigest(sections)}, {"sources", sourceProjection(context, selected)}
	};
}

Json captureAllFunctionProjection(const ParseContext &context) {
	std::map<std::string, Section *> sections;
	collectSections(context.mainSection, "root", sections);
	Json selected = Json::object();
	std::map<const Section *, std::string> bodies;
	for (const auto &[path, section] : sections) {
		if (section->type == SectionType::Function && !section->patternDefinitions.empty()) {
			if (!containsNestedDefinition(*section))
				selected[path] = sectionIdentity(*section);
			bodies.emplace(section, "");
		}
	}
	for (const CodeLine *line : context.codeLines) {
		for (const Section *section = line->section; section; section = section->parent) {
			auto body = bodies.find(section);
			if (body != bodies.end()) {
				body->second.append(line->rightTrimmedText);
				body->second += '\n';
			}
		}
	}
	Json projection = captureInactiveProjection(context, &selected);
	Json bodyDigests = Json::object();
	for (const auto &[path, section] : sections) {
		if (auto body = bodies.find(section); body != bodies.end())
			bodyDigests[path] = digest(body->second);
	}
	projection["bodies"] = std::move(bodyDigests);
	return projection;
}

bool functionDependenciesUnchanged(
	Section *section, const std::unordered_map<const Section *, bool> &unchanged, std::unordered_map<Section *, bool> &memo,
	std::unordered_set<Section *> &visiting
) {
	auto cached = memo.find(section);
	if (cached != memo.end())
		return cached->second;
	if (!unchanged.at(section))
		return memo[section] = false;
	if (!visiting.insert(section).second)
		return false;
	bool safe = true;
	std::function<void(const PatternMatch &)> checkMatch = [&](const PatternMatch &match) {
		for (PatternDefinition *definition : match.matchingDefinitions) {
			if (definition->section != section &&
				!functionDependenciesUnchanged(definition->section, unchanged, memo, visiting))
				safe = false;
		}
		for (const PatternMatch &subMatch : match.subMatches)
			checkMatch(subMatch);
	};
	std::function<void(Section *)> checkBody = [&](Section *body) {
		for (PatternReference *reference : body->patternReferences) {
			if (reference->match)
				checkMatch(*reference->match);
		}
		for (Section *child : body->children) {
			if (child->patternDefinitions.empty())
				checkBody(child);
		}
	};
	checkBody(section);
	for (const auto &[_, instantiation] : section->instantiations) {
		std::function<void(const InstantiatedSectionBody *)> checkInferredBody = [&](const InstantiatedSectionBody *body) {
			if (!body)
				return;
			for (Expression *expression : body->lineExpressions) {
				visitExpressionTree(expression, [&](Expression *node) {
					for (PatternDefinition *definition : {node->selectedPatternDefinition, node->selectedCallableDefinition}) {
						if (definition && definition->section != section &&
							!functionDependenciesUnchanged(definition->section, unchanged, memo, visiting))
							safe = false;
					}
					return false;
				});
			}
			for (const auto &child : body->childBodies)
				checkInferredBody(child.get());
		};
		checkInferredBody(instantiation.body.get());
	}
	visiting.erase(section);
	return memo[section] = safe;
}

bool compatibleObservations(const Json &previous, const Json &current, const ParseContext &context);

std::unordered_set<const Section *>
eligibleFunctionSections(const ParseContext &context, const Json &previous, const Json &currentObservations) {
	std::unordered_set<const Section *> eligible;
	if (context.options.optimizationLevel || context.options.emitDebugInfo || context.options.traceExecution ||
		!previous.contains("all_function_projection") || !previous.contains("function_sections") ||
		!compatibleObservations(previous, currentObservations, context))
		return eligible;
	std::map<std::string, Section *> sections;
	collectSections(context.mainSection, "root", sections);
	Json currentProjection = captureAllFunctionProjection(context);
	const Json &priorProjection = previous.at("all_function_projection");
	if (currentProjection.at("declarations") != priorProjection.at("declarations") ||
		currentProjection.at("sources") != priorProjection.at("sources") ||
		currentProjection.at("inactive") != priorProjection.at("inactive"))
		return eligible;
	const Json &previousBodies = priorProjection.at("bodies");
	const Json &currentBodies = currentProjection.at("bodies");
	std::unordered_map<const Section *, bool> unchanged;
	for (const auto &[path, section] : sections)
		unchanged[section] = !currentBodies.contains(path) || currentBodies.at(path) == previousBodies.at(path);
	std::unordered_map<Section *, bool> memo;
	std::unordered_set<Section *> visiting;
	for (const auto &[_, section] : sections) {
		if (section->type == SectionType::Function && !section->isFlex && !section->isConversion &&
			functionDependenciesUnchanged(section, unchanged, memo, visiting))
			eligible.insert(section);
	}
	return eligible;
}

bool compatibleObservations(const Json &previous, const Json &current, const ParseContext &context) {
	const Json &before = previous.at("files");
	if (before.size() != current.size())
		return false;
	std::set<std::string> imported;
	for (const auto &[canonical, _] : context.importedFiles)
		imported.insert(canonical);
	for (const auto &[path, observation] : before.items()) {
		if (!current.contains(path))
			return false;
		Json old = observation;
		if (old.is_object())
			old.erase("semantic");
		if (old == current.at(path))
			continue;
		if (!old.is_object() || !current.at(path).is_object() ||
			!imported.contains(current.at(path).at("canonical").get<std::string>()) ||
			path == context.builtinSyntaxConfigPath || path == context.projectSyntaxConfigPath)
			return false;
		old.erase("content");
		Json now = current.at(path);
		now.erase("content");
		if (old != now)
			return false;
	}
	return true;
}

bool canReuseInactiveBodies(
	const Json &previous, const Json &observations, const ParseContext &context, Json &currentProjection
) {
	if (!previous.contains("inactive_projection") || previous.at("diagnostics") != "" || context.options.emitDebugInfo ||
		context.options.traceExecution || !context.diagnostics.empty() || !context.scopeRuleWarnings.empty() ||
		!compatibleObservations(previous, observations, context))
		return false;
	const Json &priorProjection = previous.at("inactive_projection");
	currentProjection = captureInactiveProjection(context, &priorProjection.at("inactive"));
	return currentProjection.is_object() && currentProjection.at("declarations") == priorProjection.at("declarations") &&
		   currentProjection.at("sources") == priorProjection.at("sources");
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
	bool reusedTypeInference = false;
	bool linkedCachedObjects = false;
	size_t reusedInferenceInstantiations = 0;
	size_t reusedFunctionIR = 0;
	size_t reusedObjects = 0;
	size_t builtObjects = 0;
	size_t matcherCacheHits = 0;
	size_t precedenceMatcherCacheHits = 0;
	double frontendMilliseconds = 0;
	double inferenceMilliseconds = 0;
	double codegenMilliseconds = 0;
	double irGenerationMilliseconds = 0;
	double partitionMilliseconds = 0;
	double linkMilliseconds = 0;
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

void restoreCachedModule(ParseContext &context, const Json &cached, const fs::path &entry) {
	delete context.llvmModule;
	context.llvmModule = nullptr;
	llvm::SMDiagnostic diagnostic;
	std::unique_ptr<llvm::Module> module =
		llvm::parseAssemblyString(cached.at("ir").get<std::string>(), diagnostic, *context.llvmContext);
	if (!module || llvm::verifyModule(*module))
		throw std::runtime_error("invalid cached LLVM IR: " + entry.string());
	module->setModuleIdentifier(cached.at("module_id").get<std::string>());
	context.llvmModule = module.release();
}

#include "incrementalCompilationPartitions.inl"
#include "incrementalInferenceReuse.inl"
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
		bool reusePreviousFrontend = false;
		std::string previousMatches;
		std::string previousMatcherKey;
		if (auto cached = readEntry(programEntry)) {
			previous = Json::parse(*cached);
			reusePreviousFrontend = observationsStillEquivalent(*previous, *recording, context.options);
			if (!reusePreviousFrontend)
				previousMatcherKey = previous->at("matcher_key").get<std::string>();
		}
		if (reusePreviousFrontend) {
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
				restoreCachedModule(context, *previous, programEntry);
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
			auto stageStart = Clock::now();
			bool compiled = compileBeforeTypeInference(context.options.inputPath, context);
			statistics.frontendMilliseconds = elapsedMilliseconds(stageStart);
			Json inactiveProjection;
			bool reuseTypeInference =
				compiled && previous && canReuseInactiveBodies(*previous, recording->observations, context, inactiveProjection);
			std::map<const Section *, std::string> sectionPaths;
			std::unique_ptr<IncrementalIRReuse> functionReuse;
			std::unique_ptr<IncrementalInferenceReuse> inferenceReuse;
			if (compiled && !reuseTypeInference) {
				std::map<std::string, Section *> sections;
				collectSections(context.mainSection, "root", sections);
				for (const auto &[path, section] : sections)
					sectionPaths.emplace(section, path);
				if (previous) {
					std::unordered_set<const Section *> eligible =
						eligibleFunctionSections(context, *previous, recording->observations);
					if (!eligible.empty()) {
						functionReuse = std::make_unique<IncrementalIRReuse>(
							context, previous->at("ir").get_ref<const std::string &>(), previous->at("function_sections"),
							sectionPaths, eligible
						);
						inferenceReuse = std::make_unique<IncrementalInferenceReuse>(
							context, previous->at("inference_summaries"), *functionReuse, std::move(eligible)
						);
						context.reuseInferredInstantiation = [&](Section *section, const InstantiationKey &key,
																 Instantiation &instantiation) {
							return inferenceReuse->reuse(section, key, instantiation);
						};
					}
				}
			}
			stageStart = Clock::now();
			if (compiled && !reuseTypeInference)
				compiled = finishTypeInference(context);
			statistics.inferenceMilliseconds = elapsedMilliseconds(stageStart);
			statistics.reusedInferenceInstantiations = inferenceReuse ? inferenceReuse->reusedCount() : 0;
			context.reuseInferredInstantiation = {};
			statistics.matcherCacheHits = matcherCache.hits();
			statistics.precedenceMatcherCacheHits = matcherCache.precedenceHits();
			context.reuseMatcherResult = {};
			context.recordMatcherResult = {};
			auto saveMatcher = [&]() {
				std::string payload = matcherCache.snapshot();
				std::string key = digest(payload);
				writeEntry(directory / ("matcher-" + key + ".entry"), payload);
				return key;
			};
			if (reuseTypeInference) {
				statistics.reusedTypeInference = true;
				std::vector<std::string> libraries = previous->at("libraries").get<std::vector<std::string>>();
				context.requiredLibraries.insert(libraries.begin(), libraries.end());
				std::vector<std::string> objectKeys = previous->at("object_keys").get<std::vector<std::string>>();
				std::optional<bool> cachedLink = linkCachedObjects(context, directory, objectKeys, libraries, statistics);
				if (cachedLink) {
					success = *cachedLink;
					statistics.objectKeys = objectKeys;
				} else {
					restoreCachedModule(context, *previous, programEntry);
					success = emitPartitions(context, directory, compilerIdentity, options, libraries, statistics);
				}
				if (success) {
					Json result = *previous;
					result["files"] = recording->observations;
					result["inactive_projection"] = std::move(inactiveProjection);
					result["matcher_key"] = saveMatcher();
					result["object_keys"] = statistics.objectKeys;
					recordSemanticSources(result, *recording, context);
					writeEntry(programEntry, result.dump());
				}
			} else if (compiled) {
				std::map<std::string, std::string> functionSections;
				context.recordGeneratedFunction = [&](Section *section, llvm::Function *function) {
					functionSections.emplace(function->getName().str(), sectionPaths.at(section));
				};
				if (functionReuse) {
					context.reuseGeneratedFunction = [&](Section *section, Instantiation &instantiation,
														 llvm::Function *function) {
						return functionReuse->reuse(section, instantiation, function);
					};
					context.importReusedFunctions = [&](ParseContext &compiled) {
						return functionReuse->import(compiled);
					};
				}
				stageStart = Clock::now();
				success = generateCode(context, [&](ParseContext &compiled) {
					statistics.irGenerationMilliseconds = elapsedMilliseconds(stageStart);
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
					result["matcher_key"] = saveMatcher();
					result["all_function_projection"] = captureAllFunctionProjection(compiled);
					if (functionReuse) {
						for (const std::string &name : functionReuse->reusedFunctionNames())
							functionSections.emplace(name, previous->at("function_sections").at(name).get<std::string>());
					}
					result["function_sections"] = functionSections;
					std::set<std::string> generatedFunctionPaths;
					for (const auto &[_, path] : functionSections)
						generatedFunctionPaths.insert(path);
					result["inactive_projection"] = captureInactiveProjection(compiled, nullptr, &generatedFunctionPaths);
					result["inference_summaries"] = inferenceReuse ? inferenceReuse->snapshot(sectionPaths, diagnosticSources)
																   : captureInferenceSummaries(sectionPaths, diagnosticSources);
					recordSemanticSources(result, *recording, compiled);
					bool reusedGeneratedCode = false;
					if (previous && previous->at("ir") == result.at("ir")) {
						std::vector<std::string> priorObjects = previous->at("object_keys").get<std::vector<std::string>>();
						std::optional<bool> cachedLink =
							linkCachedObjects(compiled, directory, priorObjects, libraries, statistics);
						if (cachedLink) {
							if (!*cachedLink)
								return false;
							statistics.objectKeys = std::move(priorObjects);
							reusedGeneratedCode = true;
						}
					}
					if (!reusedGeneratedCode &&
						!emitPartitions(compiled, directory, compilerIdentity, options, libraries, statistics))
						return false;
					result["object_keys"] = statistics.objectKeys;
					writeEntry(programEntry, result.dump());
					return true;
				});
				statistics.codegenMilliseconds = elapsedMilliseconds(stageStart);
				statistics.reusedFunctionIR = functionReuse ? functionReuse->reusedCount() : 0;
				context.recordGeneratedFunction = {};
				context.reuseGeneratedFunction = {};
				context.importReusedFunctions = {};
			}
		}
		trimCache(directory);
		if (showStatistics)
			std::cerr << "incremental: "
					  << Json{{"frontend_reused", statistics.reusedFrontend},
							  {"type_inference_reused", statistics.reusedTypeInference},
							  {"inference_instantiations_reused", statistics.reusedInferenceInstantiations},
							  {"functions_ir_reused", statistics.reusedFunctionIR},
							  {"linked_cached_objects", statistics.linkedCachedObjects},
							  {"objects_reused", statistics.reusedObjects},
							  {"objects_built", statistics.builtObjects},
							  {"matcher_cache_hits", statistics.matcherCacheHits},
							  {"precedence_matcher_cache_hits", statistics.precedenceMatcherCacheHits},
							  {"frontend_ms", statistics.frontendMilliseconds},
							  {"import_ms", context.frontendStageMilliseconds[0]},
							  {"parse_ms", context.frontendStageMilliseconds[1]},
							  {"resolve_ms", context.frontendStageMilliseconds[2]},
							  {"validate_target_ms", context.frontendStageMilliseconds[3]},
							  {"inference_ms", statistics.inferenceMilliseconds},
							  {"ir_generation_ms", statistics.irGenerationMilliseconds},
							  {"partition_ms", statistics.partitionMilliseconds},
							  {"link_ms", statistics.linkMilliseconds},
							  {"codegen_and_link_ms", statistics.codegenMilliseconds}}
							 .dump()
					  << '\n';
		return success;
	} catch (const std::exception &error) {
		std::cerr << "Incremental compilation failed: " << error.what() << '\n';
		return false;
	}
}
