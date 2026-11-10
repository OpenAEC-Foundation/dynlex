#include "incrementalCompilation.h"
#include "codegen/codegen.h"
#include "codegen/native.h"
#include "codegen/targetOptions.h"
#include "compiler.h"
#include "expression.h"
#include "incrementalIRReuse.h"
#include "incrementalMatcherCache.h"
#include "incrementalModuleBitcode.h"
#include "lsp/fileSystem.h"
#include "lsp/sourceFile.h"
#include "pathUtils.h"
#include "patternTreeNode.h"
#include "section.h"
#include "section/classSection.h"
#include "section/variable.h"
#include "syntaxConfig.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Analysis/ModuleSummaryAnalysis.h"
#include "llvm/Analysis/ProfileSummaryInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalIFunc.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Mangler.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/LTO/LTO.h"
#include "llvm/Linker/Linker.h"
#include "llvm/Object/BuildID.h"
#include "llvm/Object/ModuleSymbolTable.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/BLAKE3.h"
#include "llvm/Support/Caching.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/SHA256.h"
#include "llvm/Support/Threading.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/Transforms/IPO/StripDeadPrototypes.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <stack>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
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

#include "incrementalCompilerIdentity.inl"

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

bool recordCacheAccess(const fs::path &path) {
	std::error_code error;
	fs::last_write_time(path, fs::file_time_type::clock::now(), error);
	if (error && error != std::errc::no_such_file_or_directory)
		throw fs::filesystem_error("cannot record cache access", path, error);
	return !error;
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
	// Cache eviction tracks successful access, including reused optimization
	// units whose contents need no rewrite during an edited build.
	recordCacheAccess(path);
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

#include "incrementalProgramCache.inl"

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

#include "incrementalResolvedDeclaration.inl"

bool reusableCodegenSection(const Section &section) {
	if (section.type == SectionType::Retain || section.type == SectionType::Release)
		return !containsNestedDefinition(section);
	return section.type == SectionType::Function && !section.patternDefinitions.empty() && !section.isFlex &&
		   !section.isConversion && !containsNestedDefinition(section);
}

bool sourceProjectionsEqual(const Json &left, const Json &right);

// These implementation fragments depend on declarations from earlier fragments.
// clang-format off
#include "incrementalFunctionMatches.inl"
#include "incrementalMatcherTreeSignature.inl"
#include "incrementalPrecedenceRules.inl"
#include "incrementalDeclarationDigest.inl"
#include "incrementalSourceProjection.inl"
#include "incrementalInactiveProjection.inl"

bool hasNoGlobalAccess(const Section *section);

#include "incrementalFunctionProjection.inl"
#include "incrementalExecutionProjection.inl"
#include "incrementalInactiveBodyResolution.inl"
#include "incrementalMatcherProjection.inl"
// clang-format on

#include "incrementalSectionDependencies.inl"

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
		collectMatchedSectionDependencies(node, pending);
		for (const auto &[_, instantiation] : node->instantiations)
			collectInferredSectionDependencies(instantiation.body.get(), pending);
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

#include "incrementalDeferredMatchReplay.inl"
#include "incrementalInactiveFunctionProjection.inl"
#include "incrementalObservationReuse.inl"

#include "incrementalOptions.inl"

std::string renderModule(const llvm::Module &module) {
	std::string result;
	llvm::raw_string_ostream output(result);
	module.print(output, nullptr);
	return result;
}

#include "incrementalLinkedOutput.inl"

#include "incrementalCacheTrim.inl"

// clang-format off: the native fragments share declarations in this order.
#include "incrementalNativeObjects.inl"
#include "incrementalNativeLayout.inl"
#include "incrementalNativeSymbols.inl"
#include "incrementalNativeDependencies.inl"
// clang-format on

#include "incrementalCompilationPartitions.inl"
#include "incrementalInferenceTypes.inl"
// clang-format off: the optimizer consumes the input descriptors.
#include "incrementalOptimizationInputs.inl"
#include "incrementalOptimization.inl"
#include "incrementalOptimizedNativeUnits.inl"
// clang-format on

#include "incrementalInferenceEffects.inl"
#include "incrementalInferenceReuse.inl"

// clang-format off: these includes depend on the declarations in the preceding files.
#include "incrementalDependencyGraph.inl"
#include "incrementalConstraintValue.inl"
#include "incrementalClassInstantiations.inl"
#include "incrementalConstraintProbeReuse.inl"
// clang-format on

#include "incrementalInferencePreinfer.inl"
#include "incrementalStatisticsReport.inl"
} // namespace

bool compileIncrementally(
	ParseContext &context, const std::string &cacheDirectory, const std::string &compilerPath, bool showStatistics
) {
	try {
		auto compileStart = Clock::now();
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
		statistics.cacheDirectory = directory.string();
		statistics.cacheEntry = programEntry.string();
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
			if (auto cached = readProgramManifest(programEntry)) {
				previous = std::move(*cached);
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
		statistics.cacheLoadMilliseconds = elapsedMilliseconds(compileStart);
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
			std::cerr << cachedDiagnostics;
			if (linkedOutputUnchanged(context, *previous)) {
				success = true;
				statistics.objectKeys = previous->at("object_keys").get<std::vector<std::string>>();
			} else {
				context.llvmContext = new llvm::LLVMContext();
				context.llvmModule = new llvm::Module("dynlex_cached_link", *context.llvmContext);
				std::string error;
				context.targetMachine = createNativeTargetMachine(context, error);
				if (!context.targetMachine)
					throw std::runtime_error(error);
				std::vector<std::string> libraries = previous->at("libraries").get<std::vector<std::string>>();
				context.requiredLibraries.insert(libraries.begin(), libraries.end());
				success = linkOrRestoreNativeOutput(
					context, directory, programEntry, compilerIdentity, options, libraries, statistics, *previous
				);
			}
			if (success &&
				(configOnlyChange || !statistics.nativeUnits.is_null() || !linkedOutputUnchanged(context, *previous))) {
				Json result = *previous;
				recordNativeOutput(result, statistics, &*previous);
				result["linked_output"] = linkedOutputIdentity(context);
				if (configOnlyChange) {
					result["files"] = recording->observations;
					result["diagnostics"] = cachedDiagnostics;
					recordSemanticSources(result, *recording, context);
				}
				writeProgramEntry(programEntry, std::move(result), &*previous);
			}
		} else {
			if (previous) {
				auto payloadStart = Clock::now();
				statistics.cachePayloadsLoaded += hydrateProgramField(*previous, directory, "all_function_projection");
				statistics.cachePayloadLoadMilliseconds += elapsedMilliseconds(payloadStart);
			}
			auto restoreStart = Clock::now();
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
			context.reuseParsedExpression = [&](Section *section, Range range, SectionType patternType,
												bool registerReferences) {
				return parserCache.reuse(section, range, patternType, registerReferences);
			};
			context.recordParsedExpression = [&](Range range, SectionType patternType, bool registerReferences,
												 const Expression *expression) {
				parserCache.record(range, patternType, registerReferences, expression);
			};
			context.retainParsedTemporaryExpression = [&](Range range, SectionType patternType) {
				parserCache.retainTemporary(range, patternType);
			};
			context.reuseParsedDefinition = [&](Range range, std::vector<DefinitionPatternElement> &elements) {
				return parserCache.reuseDefinition(range, elements);
			};
			context.recordParsedDefinition = [&](Range range, const std::vector<DefinitionPatternElement> &elements) {
				parserCache.recordDefinition(range, elements);
			};
			IncrementalMatcherCache matcherCache(context, previousMatches);
			std::unordered_map<const Section *, const Json *> inactiveMatchEntries;
			context.selectInactiveBodiesForResolutionReuse = [&]() {
				if (!previous || previousMatches.empty() || context.options.emitDebugInfo || context.options.traceExecution ||
					!previous->contains("inactive_projection") ||
					!compatibleObservations(*previous, recording->observations, context))
					return std::vector<Section *>{};
				auto selectionStart = Clock::now();
				std::vector<Section *> selected = selectInactiveBodiesForResolutionReuse(
					context, recording->observations, previous->at("all_function_projection"),
					previous->at("inactive_projection"), previous->at("diagnostic_sources").get<std::set<std::string>>(),
					matcherCache, inactiveMatchEntries
				);
				statistics.inactiveBodySelectionMilliseconds += elapsedMilliseconds(selectionStart);
				return selected;
			};
			context.inactiveBodyMatcherTreeUnchanged = [&]() {
				return previous &&
					   matcherTreeSignature(context) ==
						   previous->at("all_function_projection").at("initial_matcher_tree").get_ref<const std::string &>();
			};
			context.retainInactiveBodyMatcherResults = [&](const std::vector<Section *> &sections) {
				for (const Section *section : sections)
					for (const Json &entry : *inactiveMatchEntries.at(section))
						for (const char *field : {"key", "initial_key"})
							if (entry.at(field).is_string())
								matcherCache.retain(entry.at(field).get_ref<const std::string &>());
			};
			std::string initialMatcherTree;
			Json initialMatcherRoots;
			InitialMatcherProjection initialMatcherProjection;
			PriorMatchKeys precedenceKeys;
			std::vector<BodyResolutionUnit> bodyUnits;
			context.reuseMatcherResult = [&](PatternReference *reference, MatchOptions options,
											 MatchDependencies *dependencies) {
				return matcherCache.reuse(reference, options, dependencies);
			};
			context.recordMatcherResult = [&](PatternReference *reference, MatchOptions options, const PatternMatch *match,
											  const MatchDependencies *dependencies) {
				matcherCache.record(reference, options, match, dependencies);
			};
			context.prepareDeferredMatcherReplay = [&](const std::list<PatternReference *> &references) {
				auto projectionStart = Clock::now();
				const Json *prior = previous ? &previous->at("all_function_projection").at("matcher_projection") : nullptr;
				initialMatcherProjection.capture(context, recording->observations, inactiveMatchEntries, references, prior);
				statistics.matcherProjectionMilliseconds += elapsedMilliseconds(projectionStart);
				initialMatcherTree = matcherTreeSignature(context);
				initialMatcherRoots = matcherTreeMutationSnapshot(context);
				if (previous)
					prepareDeferredMatchReplay(
						context, *previous, recording->observations, initialMatcherTree, initialMatcherRoots,
						initialMatcherProjection, precedenceKeys, bodyUnits
					);
			};
			context.restoreDeferredBodyUnits = [&]() {
				return restoreBodyResolutionUnits(context, matcherCache, bodyUnits);
			};
			context.preparePrecedenceMatcherReplay = [&](const std::vector<PatternReference *> &references) {
				initialMatcherProjection.captureBodyUnits(matcherCache);
				if (previous)
					preparePrecedenceMatchReplay(
						context, previous->at("all_function_projection"), matcherCache, precedenceKeys, references,
						statistics.precedenceTreeSignatureMilliseconds, statistics.precedenceSignatureMilliseconds,
						statistics.precedenceRuleSignatureMilliseconds, statistics.precedenceSignaturesReused
					);
			};
			statistics.cacheRestoreMilliseconds = elapsedMilliseconds(restoreStart);
			auto saveMatcher = [&]() {
				auto saveStart = Clock::now();
				std::string payload = matcherCache.snapshot();
				std::string key = digest(payload);
				writeEntry(directory / ("matcher-" + key + ".entry"), payload);
				statistics.matcherSnapshotMilliseconds += elapsedMilliseconds(saveStart);
				return key;
			};
			auto saveParser = [&]() {
				auto saveStart = Clock::now();
				std::string payload = parserCache.snapshot();
				std::string key = digest(payload);
				writeEntry(directory / ("parser-" + key + ".entry"), payload);
				statistics.parserSnapshotMilliseconds += elapsedMilliseconds(saveStart);
				return key;
			};
			auto stageStart = Clock::now();
			bool compiled = compileBeforeTypeInference(context.options.inputPath, context);
			statistics.frontendMilliseconds = elapsedMilliseconds(stageStart);
			std::map<std::string, Section *> sections;
			if (compiled)
				collectSections(context.mainSection, "root", sections);
			std::string frontendDiagnosticText;
			std::set<std::string> frontendDiagnosticSources;
			Json frontendDiagnosticRecords =
				compiled ? captureCachedDiagnostics(context, frontendDiagnosticText, frontendDiagnosticSources) : Json::array();
			size_t frontendDiagnosticCount = context.diagnostics.size();
			Json inactiveProjection;
			stageStart = Clock::now();
			bool reuseTypeInference = compiled && previous &&
									  canReuseInactiveBodies(
										  *previous, recording->observations, context, sections, frontendDiagnosticRecords,
										  inactiveProjection, &statistics.sourceProjectionWork
									  );
			statistics.inactiveProjectionMilliseconds = elapsedMilliseconds(stageStart);
			stageStart = Clock::now();
			std::optional<Json> refreshedProjection;
			if (reuseTypeInference)
				refreshedProjection = refreshInactiveFunctionProjection(
					context, recording->observations, sections, matcherCache, previous->at("all_function_projection"),
					previous->at("inactive_projection"), statistics.inactiveProjectionMatchesRechecked,
					statistics.sourceProjectionWork.matchEntriesReused
				);
			if (compiled && !refreshedProjection && previous &&
				compatibleObservations(*previous, recording->observations, context))
				refreshedProjection = refreshTopLevelExecutionProjection(
					context, recording->observations, sections, matcherCache, initialMatcherTree,
					previous->at("all_function_projection"), &statistics.sourceProjectionWork,
					statistics.topLevelProjectionSectionsReused
				);
			Json resolvedFunctionProjection = nullptr;
			if (compiled && refreshedProjection)
				resolvedFunctionProjection = std::move(*refreshedProjection);
			else if (compiled)
				resolvedFunctionProjection = captureAllFunctionProjection(
					context, recording->observations, sections, matcherCache, initialMatcherTree, initialMatcherRoots,
					previous ? &*previous : nullptr, &statistics.sourceProjectionWork,
					previous && compatibleObservations(*previous, recording->observations, context)
				);
			if (compiled) {
				auto projectionStart = Clock::now();
				resolvedFunctionProjection["matcher_projection"] = initialMatcherProjection.finish(resolvedFunctionProjection);
				statistics.matcherProjectionMilliseconds += elapsedMilliseconds(projectionStart);
				const Json *priorLiteralProjection =
					previous && compatibleObservations(*previous, recording->observations, context) &&
							initialMatcherTree == previous->at("all_function_projection")
													  .at("initial_matcher_tree")
													  .get_ref<const std::string &>()
						? &previous->at("all_function_projection")
						: nullptr;
				refreshReferenceLiteralIndex(
					resolvedFunctionProjection, context, recording->observations, sections, priorLiteralProjection
				);
			}
			statistics.projectionMilliseconds = elapsedMilliseconds(stageStart);
			std::map<const Section *, std::string> sectionPaths;
			for (const auto &[path, section] : sections)
				sectionPaths.emplace(section, path);
			std::unique_ptr<IncrementalIRReuse> functionReuse;
			std::unique_ptr<IncrementalInferenceReuse> inferenceReuse;
			std::unordered_set<const Section *> graphStable;
			if (compiled && !reuseTypeInference) {
				if (previous) {
					auto payloadStart = Clock::now();
					statistics.cachePayloadsLoaded += hydrateProgramField(*previous, directory, "section_dependency_graph");
					statistics.cachePayloadsLoaded += hydrateProgramField(*previous, directory, "inference_summaries");
					statistics.cachePayloadsLoaded += hydrateProgramField(*previous, directory, "generated_ir");
					statistics.cachePayloadLoadMilliseconds += elapsedMilliseconds(payloadStart);
					if (compatibleObservations(*previous, recording->observations, context))
						prepareConstraintProbeReuse(
							context, previous->at("all_function_projection"), resolvedFunctionProjection,
							previous->at("section_dependency_graph"), sections
						);
					IncrementalPreinferPlan plan = incrementalInferenceCandidates(
						context, *previous, recording->observations, sectionPaths, resolvedFunctionProjection
					);
					statistics.preinferDependencyGraphSectionsChecked = plan.dependencyGraphSectionsChecked;
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
							inferred, *previous, recording->observations, resolvedFunctionProjection, sections, stable,
							&statistics.dependencyGraphSectionsReused
						);
						statistics.preinferenceMilliseconds = elapsedMilliseconds(preinferStart);
						if (eligible.empty())
							return;
						graphStable = eligible;
						std::unordered_set<std::string> replacementPaths;
						for (const Section *section : stable)
							replacementPaths.insert(sectionPaths.at(section));
						functionReuse = std::make_unique<IncrementalIRReuse>(
							inferred, previous->at("generated_ir").get_ref<const std::string &>(),
							previous->at("function_sections"), previous->at("callable_functions"),
							previous->at("function_instantiations"), sectionPaths, eligible, std::move(replacementPaths)
						);
						inferenceReuse = std::make_unique<IncrementalInferenceReuse>(
							priorSummaries, previous->at("global_types"), *functionReuse, sectionPaths, std::move(eligible)
						);
						inferred.reuseInferredInstantiation = [&](Section *section, const InstantiationKey &key,
																  Instantiation &instantiation, bool allowPureValueSummary) {
							return inferenceReuse->reuse(section, key, instantiation, allowPureValueSummary);
						};
					};
				}
			}
			stageStart = Clock::now();
			if (compiled && !reuseTypeInference)
				compiled = finishTypeInference(context);
			statistics.inferenceMilliseconds = elapsedMilliseconds(stageStart);
			if (compiled) {
				if (reuseTypeInference) {
					const Json &inferred = previous->at("all_function_projection");
					for (const char *field : {"constraint_probe_schedule", "class_instantiations"})
						resolvedFunctionProjection[field] = inferred.at(field);
				} else {
					resolvedFunctionProjection["constraint_probe_schedule"] =
						captureConstraintProbeSchedule(context, sectionPaths);
					resolvedFunctionProjection["class_instantiations"] = captureClassInstantiations(sectionPaths);
				}
			}
			statistics.expressionParseHits = parserCache.hits();
			statistics.temporaryExpressionParseHits = parserCache.temporaryHitCount();
			statistics.expressionParseMisses = parserCache.missCount();
			statistics.definitionParseHits = parserCache.definitionHitCount();
			statistics.definitionParseMisses = parserCache.definitionMissCount();
			context.reuseParsedExpression = {};
			context.recordParsedExpression = {};
			context.retainParsedTemporaryExpression = {};
			context.reuseParsedDefinition = {};
			context.recordParsedDefinition = {};
			statistics.reusedInferenceInstantiations = inferenceReuse ? inferenceReuse->reusedCount() : 0;
			context.reuseInferredInstantiation = {};
			context.runIncrementalPreinference = {};
			statistics.matcherCacheHits = matcherCache.hits();
			statistics.deferredMatchKeysReplayed = matcherCache.deferredKeyReplays();
			statistics.precedenceMatchKeysReplayed = matcherCache.precedenceKeyReplays();
			statistics.precedenceMatcherCacheHits = matcherCache.precedenceHits();
			statistics.matcherKeyMilliseconds = matcherCache.keyTimeMilliseconds();
			statistics.matcherRestoreMilliseconds = matcherCache.restoreTimeMilliseconds();
			context.reuseMatcherResult = {};
			context.recordMatcherResult = {};
			context.prepareDeferredMatcherReplay = {};
			context.restoreDeferredBodyUnits = {};
			context.preparePrecedenceMatcherReplay = {};
			if (reuseTypeInference) {
				statistics.reusedTypeInference = true;
				std::vector<std::string> libraries = previous->at("libraries").get<std::vector<std::string>>();
				context.requiredLibraries.insert(libraries.begin(), libraries.end());
				if (linkedOutputUnchanged(context, *previous)) {
					success = true;
					statistics.objectKeys = previous->at("object_keys").get<std::vector<std::string>>();
				} else {
					success = linkOrRestoreNativeOutput(
						context, directory, programEntry, compilerIdentity, options, libraries, statistics, *previous
					);
				}
				if (success) {
					std::cerr << previous->at("diagnostics").get_ref<const std::string &>();
					context.diagnostics.clear();
					statistics.reusedDiagnosticRecords = previous->at("diagnostic_records").size();
					Json result = *previous;
					result["files"] = recording->observations;
					result["inactive_projection"] = std::move(inactiveProjection);
					result["all_function_projection"] = std::move(resolvedFunctionProjection);
					result["frontend_diagnostic_records"] = std::move(frontendDiagnosticRecords);
					result["matcher_key"] = saveMatcher();
					result["parser_key"] = saveParser();
					recordNativeOutput(result, statistics, &*previous);
					recordSemanticSources(result, *recording, context);
					result["linked_output"] = linkedOutputIdentity(context);
					auto writeStart = Clock::now();
					writeProgramEntry(programEntry, std::move(result), &*previous);
					statistics.cacheWriteMilliseconds = elapsedMilliseconds(writeStart);
				}
			} else if (compiled) {
				std::map<std::string, std::string> functionSections;
				Json callableFunctions = Json::object();
				Json functionInstantiations = Json::object();
				ClassIdentityPaths functionClassPaths = classIdentityPaths(sectionPaths);
				context.recordGeneratedFunction = [&](Section *section, const InstantiationKey *key, llvm::Function *function,
													  bool callable) {
					requireCompilerInvariant(
						callable == (key == nullptr), "generated function has an invalid instantiation key"
					);
					std::string name = function->getName().str();
					functionSections.emplace(name, sectionPaths.at(section));
					if (callable)
						callableFunctions[name] = true;
					else if (std::optional<std::string> identity = inferenceSummaryKey(*key, functionClassPaths))
						functionInstantiations[name] = *identity;
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
					auto irStart = Clock::now();
					sortModuleSymbols(*compiled.llvmModule);
					std::string generatedIR = serializeCachedModule(*compiled.llvmModule);
					statistics.generatedIRSnapshotMilliseconds = elapsedMilliseconds(irStart);
					auto snapshotStart = Clock::now();
					std::vector<std::string> libraries(compiled.requiredLibraries.begin(), compiled.requiredLibraries.end());
					std::string diagnostics;
					std::set<std::string> diagnosticSources;
					Json diagnosticRecords = captureCachedDiagnostics(compiled, diagnostics, diagnosticSources);
					std::set<std::string> inferenceDiagnosticSources =
						diagnosticSourcesAfter(compiled, frontendDiagnosticCount);
					Json result{
						{"files", recording->observations},
						{"generated_ir", std::move(generatedIR)},
						{"module_id", compiled.llvmModule->getModuleIdentifier()},
						{"libraries", libraries},
						{"diagnostics", diagnostics},
						{"diagnostic_records", diagnosticRecords},
						{"diagnostic_sources", diagnosticSources}
					};
					result["matcher_key"] = saveMatcher();
					result["parser_key"] = saveParser();
					result["all_function_projection"] = std::move(resolvedFunctionProjection);
					result["frontend_diagnostic_records"] = std::move(frontendDiagnosticRecords);
					if (functionReuse) {
						for (const std::string &name : functionReuse->reusedFunctionNames()) {
							functionSections.emplace(name, previous->at("function_sections").at(name).get<std::string>());
							if (previous->at("callable_functions").contains(name))
								callableFunctions[name] = true;
							if (previous->at("function_instantiations").contains(name))
								functionInstantiations[name] = previous->at("function_instantiations").at(name);
						}
					}
					result["function_sections"] = functionSections;
					result["callable_functions"] = callableFunctions;
					result["function_instantiations"] = std::move(functionInstantiations);
					std::set<std::string> generatedFunctionPaths;
					for (const auto &[_, path] : functionSections)
						generatedFunctionPaths.insert(path);
					const Json *priorInactiveSources = previous ? &previous->at("inactive_projection").at("sources") : nullptr;
					std::set<std::string> retainedActivePaths =
						retainedActiveFunctionPaths(functionReuse.get(), previous ? &*previous : nullptr);
					result["inactive_projection"] = captureInactiveProjection(
						result.at("all_function_projection").at("declarations"), compiled, recording->observations, nullptr,
						&generatedFunctionPaths, &sections, priorInactiveSources, &statistics.sourceProjectionWork, nullptr,
						&retainedActivePaths
					);
					Json globalTypes = captureGlobalTypeProjection(sectionPaths).value_or(Json(nullptr));
					result["global_types"] = globalTypes;
					result["inference_summaries"] =
						inferenceReuse ? inferenceReuse->snapshot(
											 sectionPaths, inferenceDiagnosticSources, globalTypes,
											 &statistics.reusedInferenceSummarySections
										 )
									   : captureInferenceSummaries(sectionPaths, inferenceDiagnosticSources, globalTypes);
					auto graphStart = Clock::now();
					result["section_dependency_graph"] = captureSectionDependencyGraph(
						sectionPaths, previous ? &previous->at("section_dependency_graph") : nullptr, graphStable,
						retainedGraphVariants(functionReuse.get(), previous ? &*previous : nullptr),
						&statistics.dependencyGraphEntriesReused, &statistics.dependencyGraphReverseNodesUpdated
					);
					statistics.dependencyGraphMilliseconds = elapsedMilliseconds(graphStart);
					recordSemanticSources(result, *recording, compiled);
					statistics.snapshotMilliseconds = elapsedMilliseconds(snapshotStart);
					bool emitted;
					if (previous && previous->at("generated_ir") == result.at("generated_ir")) {
						if (previous->at("libraries") == libraries && linkedOutputUnchanged(compiled, *previous)) {
							statistics.objectKeys = previous->at("object_keys").get<std::vector<std::string>>();
							emitted = true;
						} else
							emitted = linkOrRestoreNativeOutput(
								compiled, directory, programEntry, compilerIdentity, options, libraries, statistics, *previous,
								true
							);
					} else
						emitted = buildNativeOutput(
							compiled, directory, compilerIdentity, options, libraries, statistics,
							previous ? &*previous : nullptr, functionReuse.get()
						);
					if (!emitted)
						return false;
					recordNativeOutput(result, statistics, previous ? &*previous : nullptr);
					result["linked_output"] = linkedOutputIdentity(compiled);
					auto writeStart = Clock::now();
					writeProgramEntry(programEntry, std::move(result), previous ? &*previous : nullptr);
					statistics.cacheWriteMilliseconds = elapsedMilliseconds(writeStart);
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
		auto finalizeStart = Clock::now();
		if (success)
			fs::remove(failedEntry);
		trimCache(directory);
		statistics.cacheFinalizeMilliseconds = elapsedMilliseconds(finalizeStart);
		statistics.totalMilliseconds = elapsedMilliseconds(compileStart);
		if (showStatistics)
			emitIncrementalStatistics(context, statistics);
		return success;
	} catch (const std::exception &error) {
		std::cerr << "Incremental compilation failed: " << error.what() << '\n';
		return false;
	}
}
