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
#include "section/classSection.h"
#include "section/variable.h"
#include "syntaxConfig.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Object/BuildID.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/BLAKE3.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/SHA256.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/Transforms/IPO/StripDeadPrototypes.h"
#include "llvm/Transforms/Utils/SplitModule.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <stdexcept>
#include <type_traits>
#include <unordered_set>
#include <variant>

namespace {
using Json = nlohmann::json;
namespace fs = std::filesystem;
constexpr std::string_view cacheHeader = "DynLex incremental cache v1\n";
constexpr uintmax_t maximumEntryBytes = 256 * 1024 * 1024;
constexpr uintmax_t maximumCacheBytes = 512 * 1024 * 1024;
constexpr unsigned partitionCount = 16;
using Clock = std::chrono::steady_clock;

double elapsedMilliseconds(Clock::time_point start) {
	return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

std::string digest(std::string_view text) {
	llvm::BLAKE3 hash;
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
	lsp::SourceFile *cachedFile(const std::string &path) { return files.getFile(path); }
	Json observations = Json::object();

  private:
	lsp::LocalFileSystem files;
};

#include "incrementalDiagnosticCache.inl"
#include "incrementalParsedExpressionCache.inl"

void recordSemanticSources(Json &result, RecordingFileSystem &files, const ParseContext &context) {
	result["comment_prefixes"] =
		Json{{"builtin", context.builtinSyntax.commentPrefix}, {"project", context.projectSyntax.commentPrefix}};
	result["config_paths"] = Json::array({context.builtinSyntaxConfigPath, context.projectSyntaxConfigPath});
	result["syntax_structure"] =
		Json::array({context.builtinSyntax.nonMessageIdentity, context.projectSyntax.nonMessageIdentity});
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

#include "incrementalSectionIdentity.inl"

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

Json sectionDeclaration(const Section &section) {
	Json definitions = Json::array();
	for (const PatternDefinition *definition : section.patternDefinitions)
		definitions.push_back(definition->toString());
	return Json{
		{"identity", sectionIdentity(section)},
		{"definitions", definitions},
		{"local", section.isLocal},
		{"flex", section.isFlex},
		{"exposed", section.isExposed},
		{"conversion", section.isConversion},
		{"implicit_conversion", section.isImplicitConversion},
		{"return_contract", static_cast<int>(section.returnContract)},
		{"before", section.beforePatterns},
		{"after", section.afterPatterns},
		{"associativity", static_cast<int>(section.associativity)}
	};
}

bool reusableCodegenSection(const Section &section) {
	if (section.type == SectionType::Retain || section.type == SectionType::Release)
		return !containsNestedDefinition(section);
	return section.type == SectionType::Function && !section.patternDefinitions.empty() && !section.isFlex &&
		   !section.isConversion && !containsNestedDefinition(section);
}

bool sourceProjectionsEqual(const Json &left, const Json &right);

#include "incrementalFunctionMatches.inl"

std::string
declarationDigest(const std::map<std::string, Section *> &sections, const std::unordered_set<const Section *> &excluded = {}) {
	Json declarations = Json::array();
	for (const auto &[path, section] : sections) {
		if (section->patternDefinitions.empty() || excluded.contains(section))
			continue;
		Json entry = sectionDeclaration(*section);
		entry["path"] = path;
		declarations.push_back(std::move(entry));
	}
	return digest(declarations.dump());
}

#include "incrementalSourceProjection.inl"

Json captureInactiveProjection(
	const ParseContext &context, const Json &observations, const Json *priorInactive = nullptr,
	const std::set<std::string> *generatedFunctionPaths = nullptr,
	const std::map<std::string, Section *> *knownSections = nullptr, const Json *priorSources = nullptr,
	SourceProjectionWork *projectionWork = nullptr
) {
	std::map<std::string, Section *> collectedSections;
	if (!knownSections) {
		collectSections(context.mainSection, "root", collectedSections);
		knownSections = &collectedSections;
	}
	Json inactive = priorInactive ? *priorInactive : inactiveFunctionSections(*knownSections);
	if (generatedFunctionPaths) {
		for (const std::string &path : *generatedFunctionPaths)
			inactive.erase(path);
	}
	std::unordered_set<const Section *> selected;
	for (const auto &[path, identity] : inactive.items()) {
		auto section = knownSections->find(path);
		if (section == knownSections->end() || sectionIdentity(*section->second) != identity)
			return nullptr;
		selected.insert(section->second);
	}
	return Json{
		{"inactive", inactive},
		{"declarations", declarationDigest(*knownSections)},
		{"sources", sourceProjection(context, observations, selected, false, false, false, priorSources, projectionWork)}
	};
}

bool hasNoGlobalAccess(const Section *section);

#include "incrementalFunctionProjection.inl"

bool functionDependenciesUnchanged(
	Section *section, const std::unordered_map<const Section *, bool> &unchanged, std::unordered_map<Section *, bool> &memo,
	const std::unordered_set<const Section *> &stableBoundaries = {}
) {
	if (stableBoundaries.contains(section))
		return true;
	auto cached = memo.find(section);
	if (cached != memo.end())
		return cached->second;
	std::unordered_set<Section *> visited;
	std::vector<Section *> pending{section};
	bool safe = true;
	std::function<void(const PatternMatch &)> checkMatch = [&](const PatternMatch &match) {
		for (PatternDefinition *definition : match.matchingDefinitions)
			pending.push_back(definition->section);
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
	auto checkInstantiations = [&](Section *node) {
		for (const auto &[_, instantiation] : node->instantiations) {
			std::function<void(const InstantiatedSectionBody *)> checkInferredBody = [&](const InstantiatedSectionBody *body) {
				if (!body)
					return;
				for (Expression *expression : body->lineExpressions) {
					visitExpressionTree(expression, [&](Expression *node) {
						for (PatternDefinition *definition :
							 {node->selectedPatternDefinition, node->selectedCallableDefinition}) {
							if (definition)
								pending.push_back(definition->section);
						}
						return false;
					});
				}
				for (const auto &child : body->childBodies)
					checkInferredBody(child.get());
			};
			checkInferredBody(instantiation.body.get());
		}
	};
	while (!pending.empty() && safe) {
		Section *node = pending.back();
		pending.pop_back();
		if (stableBoundaries.contains(node) || !visited.insert(node).second)
			continue;
		if (auto known = memo.find(node); known != memo.end()) {
			safe = known->second;
			continue;
		}
		if (!unchanged.at(node)) {
			safe = false;
			break;
		}
		checkBody(node);
		checkInstantiations(node);
	}
	return memo[section] = safe;
}

bool compatibleObservations(const Json &previous, const Json &current, const ParseContext &context);

#include "incrementalCodegenEligibility.inl"

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
	const Json &previous, const Json &observations, const ParseContext &context,
	const std::map<std::string, Section *> &sections, Json &currentProjection, SourceProjectionWork *projectionWork
) {
	if (!previous.contains("inactive_projection") || previous.at("diagnostics") != "" || context.options.emitDebugInfo ||
		context.options.traceExecution || !context.diagnostics.empty() || !context.scopeRuleWarnings.empty() ||
		!compatibleObservations(previous, observations, context))
		return false;
	const Json &priorProjection = previous.at("inactive_projection");
	currentProjection = captureInactiveProjection(
		context, observations, &priorProjection.at("inactive"), nullptr, &sections, &priorProjection.at("sources"),
		projectionWork
	);
	return currentProjection.is_object() && currentProjection.at("declarations") == priorProjection.at("declarations") &&
		   sourceProjectionsEqual(currentProjection.at("sources"), priorProjection.at("sources"));
}

#include "incrementalObservationReuse.inl"

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

#include "incrementalLinkedOutput.inl"

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
							  : name.starts_with("parser-")	 ? 7
							  : name.starts_with("errors-")	 ? 7
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

#include "incrementalNativeObjects.inl"

#include "incrementalCompilationPartitions.inl"
#include "incrementalInferenceTypes.inl"

#include "incrementalInferenceEffects.inl"
#include "incrementalInferenceReuse.inl"

#include "incrementalInferencePreinfer.inl"
} // namespace

bool compileIncrementally(
	ParseContext &context, const std::string &cacheDirectory, const std::string &compilerPath, bool showStatistics
) {
	try {
		fs::path directory = fs::absolute(cacheDirectory) / "native-v1";
		fs::create_directories(directory);
		std::string compilerIdentity = compilerBuildIdentity(compilerPath);
		Json options = optionIdentity(context.options);
		std::string programKey = digest(compilerIdentity + options.dump());
		fs::path programEntry = directory / ("program-" + programKey + ".entry");
		fs::path failedEntry = directory / ("errors-" + programKey + ".entry");
		auto files = std::make_unique<RecordingFileSystem>();
		RecordingFileSystem *recording = files.get();
		context.fileSystem = std::move(files);
		BuildStatistics statistics;
		bool success = false;
		std::optional<Json> previous;
		std::optional<Json> previousFailure;
		bool reusePreviousFrontend = false;
		bool reuseFailedFrontend = false;
		bool configOnlyChange = false;
		std::string cachedDiagnostics;
		std::string previousMatches;
		std::string previousMatcherKey;
		std::string previousParserKey;
		if (auto cached = readEntry(failedEntry)) {
			previousFailure = Json::parse(*cached);
			if (!initializeSyntaxConfigs(context, context.options.inputPath))
				return false;
			reuseFailedFrontend =
				observationsStillEquivalent(*previousFailure, *recording, context, cachedDiagnostics, configOnlyChange);
			if (!reuseFailedFrontend)
				previousMatcherKey = previousFailure->at("matcher_key").get<std::string>();
			if (!reuseFailedFrontend)
				previousParserKey = previousFailure->at("parser_key").get<std::string>();
		}
		if (!reuseFailedFrontend) {
			if (auto cached = readEntry(programEntry)) {
				previous = Json::parse(*cached);
				if (!previousFailure && !initializeSyntaxConfigs(context, context.options.inputPath))
					return false;
				reusePreviousFrontend =
					observationsStillEquivalent(*previous, *recording, context, cachedDiagnostics, configOnlyChange);
				if (!reusePreviousFrontend && previousMatcherKey.empty())
					previousMatcherKey = previous->at("matcher_key").get<std::string>();
				if (!reusePreviousFrontend && previousParserKey.empty())
					previousParserKey = previous->at("parser_key").get<std::string>();
			}
		}
		if (reuseFailedFrontend) {
			statistics.reusedFrontend = true;
			std::cerr << cachedDiagnostics;
			if (configOnlyChange) {
				(*previousFailure)["files"] = recording->observations;
				(*previousFailure)["diagnostics"] = cachedDiagnostics;
				recordSemanticSources(*previousFailure, *recording, context);
				writeEntry(failedEntry, previousFailure->dump());
			}
		} else if (reusePreviousFrontend) {
			statistics.reusedFrontend = true;
			std::vector<std::string> objectKeys = previous->at("object_keys").get<std::vector<std::string>>();
			std::cerr << cachedDiagnostics;
			if (linkedOutputUnchanged(context, *previous)) {
				success = true;
			} else {
				context.llvmContext = new llvm::LLVMContext();
				context.llvmModule = new llvm::Module("dynlex_cached_link", *context.llvmContext);
				std::string error;
				context.targetMachine = createNativeTargetMachine(context, error);
				if (!context.targetMachine)
					throw std::runtime_error(error);
				std::vector<std::string> libraries = previous->at("libraries").get<std::vector<std::string>>();
				context.requiredLibraries.insert(libraries.begin(), libraries.end());
				std::optional<bool> cachedLink = linkCachedObjects(context, directory, objectKeys, libraries, statistics);
				if (cachedLink) {
					success = *cachedLink;
				} else {
					restoreCachedModule(context, *previous, programEntry);
					success = emitPartitions(context, directory, compilerIdentity, options, libraries, statistics);
					if (success)
						(*previous)["object_keys"] = statistics.objectKeys;
				}
			}
			if (success) {
				bool updateEntry = configOnlyChange;
				if (!linkedOutputUnchanged(context, *previous)) {
					(*previous)["linked_output"] = linkedOutputIdentity(context);
					updateEntry = true;
				}
				if (configOnlyChange) {
					(*previous)["files"] = recording->observations;
					(*previous)["diagnostics"] = cachedDiagnostics;
					recordSemanticSources(*previous, *recording, context);
				}
				if (updateEntry)
					writeEntry(programEntry, previous->dump());
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
			std::string previousParsedExpressions;
			if (!previousParserKey.empty()) {
				fs::path parserEntry = directory / ("parser-" + previousParserKey + ".entry");
				if (auto cached = readEntry(parserEntry)) {
					if (digest(*cached) != previousParserKey)
						throw std::runtime_error("corrupt parser cache entry: " + parserEntry.string());
					previousParsedExpressions = std::move(*cached);
				}
			}
			IncrementalParsedExpressionCache parserCache(context, previousParsedExpressions);
			context.reuseParsedExpression = [&](Section *section, Range range, SectionType patternType) {
				return parserCache.reuse(section, range, patternType);
			};
			context.recordParsedExpression = [&](Range range, SectionType patternType, const Expression *expression) {
				parserCache.record(range, patternType, expression);
			};
			context.reuseParsedDefinition = [&](Range range, std::vector<DefinitionPatternElement> &elements) {
				return parserCache.reuseDefinition(range, elements);
			};
			context.recordParsedDefinition = [&](Range range, const std::vector<DefinitionPatternElement> &elements) {
				parserCache.recordDefinition(range, elements);
			};
			IncrementalMatcherCache matcherCache(context, previousMatches);
			context.reuseMatcherResult = [&](PatternReference *reference, MatchOptions options,
											 MatchDependencies *dependencies) {
				return matcherCache.reuse(reference, options, dependencies);
			};
			context.recordMatcherResult = [&](PatternReference *reference, MatchOptions options, const PatternMatch *match,
											  const MatchDependencies *dependencies) {
				matcherCache.record(reference, options, match, dependencies);
			};
			auto saveMatcher = [&]() {
				std::string payload = matcherCache.snapshot();
				std::string key = digest(payload);
				writeEntry(directory / ("matcher-" + key + ".entry"), payload);
				return key;
			};
			auto saveParser = [&]() {
				std::string payload = parserCache.snapshot();
				std::string key = digest(payload);
				writeEntry(directory / ("parser-" + key + ".entry"), payload);
				return key;
			};
			auto stageStart = Clock::now();
			bool compiled = compileBeforeTypeInference(context.options.inputPath, context);
			statistics.frontendMilliseconds = elapsedMilliseconds(stageStart);
			statistics.expressionParseHits = parserCache.hits();
			statistics.expressionParseMisses = parserCache.missCount();
			statistics.definitionParseHits = parserCache.definitionHitCount();
			statistics.definitionParseMisses = parserCache.definitionMissCount();
			context.reuseParsedExpression = {};
			context.recordParsedExpression = {};
			context.reuseParsedDefinition = {};
			context.recordParsedDefinition = {};
			stageStart = Clock::now();
			std::map<std::string, Section *> sections;
			if (compiled)
				collectSections(context.mainSection, "root", sections);
			Json resolvedFunctionProjection = compiled ? captureAllFunctionProjection(
															 context, recording->observations, sections,
															 previous ? &*previous : nullptr, &statistics.sourceProjectionWork
														 )
													   : Json(nullptr);
			statistics.projectionMilliseconds = elapsedMilliseconds(stageStart);
			Json inactiveProjection;
			bool reuseTypeInference =
				compiled && previous &&
				canReuseInactiveBodies(
					*previous, recording->observations, context, sections, inactiveProjection, &statistics.sourceProjectionWork
				);
			std::map<const Section *, std::string> sectionPaths;
			for (const auto &[path, section] : sections)
				sectionPaths.emplace(section, path);
			std::unique_ptr<IncrementalIRReuse> functionReuse;
			std::unique_ptr<IncrementalInferenceReuse> inferenceReuse;
			if (compiled && !reuseTypeInference) {
				if (previous) {
					IncrementalPreinferPlan plan = scalarArgumentIncrementalCandidates(
						context, *previous, recording->observations, sectionPaths, resolvedFunctionProjection
					);
					context.runIncrementalPreinference =
						[&, plan = std::move(plan)](
							ParseContext &inferred,
							const std::function<bool(const ParseContext::IncrementalFunctionRequest &)> &inferRequest
						) {
						auto preinferStart = Clock::now();
						const Json &priorSummaries = previous->at("inference_summaries");
						std::unordered_set<const Section *> stable;
						for (size_t first = 0; first < plan.requests.size();) {
							Section *section = plan.requests[first].section;
							size_t end = first + 1;
							while (end < plan.requests.size() && plan.requests[end].section == section)
								end++;
							std::unordered_map<Section *, bool> memo;
							if (!functionDependenciesUnchanged(section, plan.bodyUnchanged, memo, stable)) {
								std::vector<ParseContext::IncrementalFunctionRequest> variants(
									plan.requests.begin() + first, plan.requests.begin() + end
								);
								for (const auto &request : variants)
									if (inferRequest(request))
										statistics.preinferredInstantiations++;
								Json currentSummaries = captureInferenceSummaries(sectionPaths, {}, Json(nullptr));
								auto newlyStable = stableIncrementalReplacements(
									variants, sectionPaths, currentSummaries, priorSummaries,
									inferred.incrementalReplacementFunctions
								);
								stable.insert(newlyStable.begin(), newlyStable.end());
							}
							first = end;
						}
						std::unordered_set<const Section *> eligible = eligibleCodegenSections(
							inferred, *previous, recording->observations, resolvedFunctionProjection, sections, stable
						);
						statistics.preinferenceMilliseconds = elapsedMilliseconds(preinferStart);
						if (eligible.empty())
							return;
						std::unordered_set<std::string> replacementPaths;
						for (const Section *section : stable)
							replacementPaths.insert(sectionPaths.at(section));
						functionReuse = std::make_unique<IncrementalIRReuse>(
							inferred, previous->at("ir").get_ref<const std::string &>(), previous->at("function_sections"),
							previous->at("callable_functions"), sectionPaths, eligible, std::move(replacementPaths)
						);
						inferenceReuse = std::make_unique<IncrementalInferenceReuse>(
							priorSummaries, previous->at("global_types"), *functionReuse, sectionPaths, std::move(eligible)
						);
						inferred.reuseInferredInstantiation = [&](Section *section, const InstantiationKey &key,
																  Instantiation &instantiation) {
							return inferenceReuse->reuse(section, key, instantiation);
						};
					};
				}
			}
			stageStart = Clock::now();
			if (compiled && !reuseTypeInference)
				compiled = finishTypeInference(context);
			statistics.inferenceMilliseconds = elapsedMilliseconds(stageStart);
			statistics.reusedInferenceInstantiations = inferenceReuse ? inferenceReuse->reusedCount() : 0;
			context.reuseInferredInstantiation = {};
			context.runIncrementalPreinference = {};
			statistics.matcherCacheHits = matcherCache.hits();
			statistics.precedenceMatcherCacheHits = matcherCache.precedenceHits();
			statistics.matcherKeyMilliseconds = matcherCache.keyTimeMilliseconds();
			statistics.matcherRestoreMilliseconds = matcherCache.restoreTimeMilliseconds();
			context.reuseMatcherResult = {};
			context.recordMatcherResult = {};
			if (reuseTypeInference) {
				statistics.reusedTypeInference = true;
				std::vector<std::string> libraries = previous->at("libraries").get<std::vector<std::string>>();
				context.requiredLibraries.insert(libraries.begin(), libraries.end());
				std::vector<std::string> objectKeys = previous->at("object_keys").get<std::vector<std::string>>();
				if (linkedOutputUnchanged(context, *previous)) {
					success = true;
					statistics.objectKeys = objectKeys;
				} else {
					std::optional<bool> cachedLink = linkCachedObjects(context, directory, objectKeys, libraries, statistics);
					if (cachedLink) {
						success = *cachedLink;
						statistics.objectKeys = objectKeys;
					} else {
						restoreCachedModule(context, *previous, programEntry);
						success = emitPartitions(context, directory, compilerIdentity, options, libraries, statistics);
					}
				}
				if (success) {
					Json result = *previous;
					result["files"] = recording->observations;
					result["inactive_projection"] = std::move(inactiveProjection);
					result["matcher_key"] = saveMatcher();
					result["parser_key"] = saveParser();
					result["object_keys"] = statistics.objectKeys;
					recordSemanticSources(result, *recording, context);
					result["linked_output"] = linkedOutputIdentity(context);
					writeEntry(programEntry, result.dump());
				}
			} else if (compiled) {
				std::map<std::string, std::string> functionSections;
				Json callableFunctions = Json::object();
				context.recordGeneratedFunction = [&](Section *section, llvm::Function *function, bool callable) {
					std::string name = function->getName().str();
					functionSections.emplace(name, sectionPaths.at(section));
					if (callable)
						callableFunctions[name] = true;
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
					Json diagnosticRecords = captureCachedDiagnostics(compiled, diagnostics, diagnosticSources);
					Json result{
						{"files", recording->observations},
						{"ir", renderModule(*compiled.llvmModule)},
						{"module_id", compiled.llvmModule->getModuleIdentifier()},
						{"libraries", libraries},
						{"diagnostics", diagnostics},
						{"diagnostic_records", diagnosticRecords},
						{"diagnostic_sources", diagnosticSources}
					};
					result["matcher_key"] = saveMatcher();
					result["parser_key"] = saveParser();
					result["all_function_projection"] = std::move(resolvedFunctionProjection);
					if (functionReuse) {
						for (const std::string &name : functionReuse->reusedFunctionNames()) {
							functionSections.emplace(name, previous->at("function_sections").at(name).get<std::string>());
							if (previous->at("callable_functions").contains(name))
								callableFunctions[name] = true;
						}
					}
					result["function_sections"] = functionSections;
					result["callable_functions"] = callableFunctions;
					std::set<std::string> generatedFunctionPaths;
					for (const auto &[_, path] : functionSections)
						generatedFunctionPaths.insert(path);
					const Json *priorInactiveSources = previous ? &previous->at("inactive_projection").at("sources") : nullptr;
					result["inactive_projection"] = captureInactiveProjection(
						compiled, recording->observations, nullptr, &generatedFunctionPaths, &sections, priorInactiveSources,
						&statistics.sourceProjectionWork
					);
					Json globalTypes = captureGlobalTypeProjection(sectionPaths).value_or(Json(nullptr));
					result["global_types"] = globalTypes;
					result["inference_summaries"] =
						inferenceReuse ? inferenceReuse->snapshot(sectionPaths, diagnosticSources, globalTypes)
									   : captureInferenceSummaries(sectionPaths, diagnosticSources, globalTypes);
					recordSemanticSources(result, *recording, compiled);
					bool reusedGeneratedCode = false;
					if (previous && previous->at("ir") == result.at("ir")) {
						std::vector<std::string> priorObjects = previous->at("object_keys").get<std::vector<std::string>>();
						if (previous->at("libraries") == libraries && linkedOutputUnchanged(compiled, *previous)) {
							statistics.objectKeys = std::move(priorObjects);
							reusedGeneratedCode = true;
						} else {
							std::optional<bool> cachedLink =
								linkCachedObjects(compiled, directory, priorObjects, libraries, statistics);
							if (cachedLink) {
								if (!*cachedLink)
									return false;
								statistics.objectKeys = std::move(priorObjects);
								reusedGeneratedCode = true;
							}
						}
					}
					if (!reusedGeneratedCode && !emitPartitions(
													compiled, directory, compilerIdentity, options, libraries, statistics,
													previous ? &*previous : nullptr
												))
						return false;
					result["object_keys"] = statistics.objectKeys;
					result["linked_output"] = linkedOutputIdentity(compiled);
					writeEntry(programEntry, result.dump());
					return true;
				});
				statistics.codegenMilliseconds = elapsedMilliseconds(stageStart);
				statistics.reusedFunctionIR = functionReuse ? functionReuse->reusedCount() : 0;
				context.recordGeneratedFunction = {};
				context.reuseGeneratedFunction = {};
				context.importReusedFunctions = {};
			} else if (context.compilationStage >= ParseContext::CompilationStage::ImportedFiles &&
					   !context.diagnostics.empty()) {
				std::string diagnostics;
				std::set<std::string> diagnosticSources;
				Json records = captureCachedDiagnostics(context, diagnostics, diagnosticSources);
				Json result{
					{"files", recording->observations},
					{"diagnostics", diagnostics},
					{"diagnostic_records", std::move(records)},
					{"diagnostic_sources", std::move(diagnosticSources)},
					{"matcher_key", saveMatcher()},
					{"parser_key", saveParser()}
				};
				recordSemanticSources(result, *recording, context);
				writeEntry(failedEntry, result.dump());
			}
		}
		if (success)
			fs::remove(failedEntry);
		trimCache(directory);
		if (showStatistics)
			std::cerr << "incremental: "
					  << Json{{"frontend_reused", statistics.reusedFrontend},
							  {"type_inference_reused", statistics.reusedTypeInference},
							  {"inference_instantiations_reused", statistics.reusedInferenceInstantiations},
							  {"preinferred_instantiations", statistics.preinferredInstantiations},
							  {"functions_ir_reused", statistics.reusedFunctionIR},
							  {"linked_cached_objects", statistics.linkedCachedObjects},
							  {"objects_reused", statistics.reusedObjects},
							  {"objects_built", statistics.builtObjects},
							  {"matcher_cache_hits", statistics.matcherCacheHits},
							  {"precedence_matcher_cache_hits", statistics.precedenceMatcherCacheHits},
							  {"expression_parse_hits", statistics.expressionParseHits},
							  {"expression_parse_misses", statistics.expressionParseMisses},
							  {"definition_parse_hits", statistics.definitionParseHits},
							  {"definition_parse_misses", statistics.definitionParseMisses},
							  {"body_resolution_reference_visits", context.bodyResolutionReferenceVisits},
							  {"source_projections_reused", statistics.sourceProjectionWork.reused},
							  {"source_projections_scanned", statistics.sourceProjectionWork.scanned},
							  {"source_projection_mask_ms", statistics.sourceProjectionWork.maskMilliseconds},
							  {"source_projection_files_ms", statistics.sourceProjectionWork.sourceMilliseconds},
							  {"projection_setup_ms", statistics.sourceProjectionWork.setupMilliseconds},
							  {"projection_body_ms", statistics.sourceProjectionWork.bodyMilliseconds},
							  {"projection_declarations_ms", statistics.sourceProjectionWork.declarationMilliseconds},
							  {"projection_matches_ms", statistics.sourceProjectionWork.matchMilliseconds},
							  {"projection_inactive_ms", statistics.sourceProjectionWork.inactiveMilliseconds},
							  {"projection_ambient_ms", statistics.sourceProjectionWork.ambientMilliseconds},
							  {"projection_compatibility_ms", statistics.sourceProjectionWork.compatibilityMilliseconds},
							  {"matcher_key_ms", statistics.matcherKeyMilliseconds},
							  {"matcher_restore_ms", statistics.matcherRestoreMilliseconds},
							  {"frontend_ms", statistics.frontendMilliseconds},
							  {"import_ms", context.frontendStageMilliseconds[0]},
							  {"parse_ms", context.frontendStageMilliseconds[1]},
							  {"resolve_ms", context.frontendStageMilliseconds[2]},
							  {"validate_target_ms", context.frontendStageMilliseconds[3]},
							  {"inference_ms", statistics.inferenceMilliseconds},
							  {"projection_ms", statistics.projectionMilliseconds},
							  {"preinference_ms", statistics.preinferenceMilliseconds},
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
