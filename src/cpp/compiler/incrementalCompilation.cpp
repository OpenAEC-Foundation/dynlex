#include "incrementalCompilation.h"
#include "bindingResolution.h"
#include "classInstantiationRequest.h"
#include "codegen/codegen.h"
#include "codegen/native.h"
#include "codegen/targetOptions.h"
#include "compileTimeValue.h"
#include "compiler.h"
#include "conversionLookup.h"
#include "dataSections.h"
#include "expression.h"
#include "expressionCapture.h"
#include "incrementalClassRestoration.h"
#include "incrementalIRReuse.h"
#include "incrementalMatcherCache.h"
#include "incrementalModuleBitcode.h"
#include "incrementalModulePartition.h"
#include "incrementalUnitSelection.h"
#include "inferenceFailure.h"
#include "inferenceProductHooks.h"
#include "lsp/fileSystem.h"
#include "lsp/sourceFile.h"
#include "numberScopeRules.h"
#include "pathUtils.h"
#include "patternTreeNode.h"
#include "section.h"
#include "section/classSection.h"
#include "section/variable.h"
#include "sectionIdentity.h"
#include "syntaxConfig.h"
#include "validation.h"
#include "variableBindings.h"
#include "llvm/ADT/Hashing.h"
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
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Process.h"
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
using Clock = std::chrono::steady_clock;

double elapsedMilliseconds(Clock::time_point start) {
	return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

double processCPUMilliseconds() {
	llvm::sys::TimePoint<> elapsed;
	std::chrono::nanoseconds user, system;
	llvm::sys::Process::GetTimeUsage(elapsed, user, system);
	return std::chrono::duration<double, std::milli>(user + system).count();
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

#include "incrementalSourceObservations.inl"

#include "incrementalDiagnosticCache.inl"
#include "incrementalParsedExpressionCache.inl"

void recordSemanticSources(Json &result, RecordingFileSystem &files, const ParseContext &context) {
	result["comment_prefixes"] =
		Json{{"builtin", context.builtinSyntax.commentPrefix}, {"project", context.projectSyntax.commentPrefix}};
	result["configuration_files"] = files.configurationObservations;
	result["syntax_structure"] =
		Json::array({context.builtinSyntax.nonMessageIdentity, context.projectSyntax.nonMessageIdentity});
	for (auto &[path, observation] : result["files"].items()) {
		if (observation.is_null() || fs::path(path).extension() != ".dl")
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
			section->isExposed || !section->instantiations.empty() || containsNestedDefinition(*section))
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
		   !containsNestedDefinition(section);
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
#include "incrementalClassificationLiterals.inl"
#include "incrementalMatcherProjection.inl"
// clang-format on

#include "incrementalInferenceReads.inl"
#include "incrementalSectionDependencies.inl"

bool compatibleObservations(const Json &previous, const Json &current, const ParseContext &context);

#include "incrementalRootBindings.inl"

bool compatibleObservations(const Json &previous, const Json &current, const ParseContext &context) {
	const Json &before = previous.at("files");
	if (before.size() != current.size() ||
		previous.at("syntax_structure") !=
			Json::array({context.builtinSyntax.nonMessageIdentity, context.projectSyntax.nonMessageIdentity}))
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
			!imported.contains(current.at(path).at("canonical").get<std::string>()))
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
#include "incrementalInitialParameterCounts.inl"
// clang-format off
#include "incrementalClassification.inl"
// clang-format on
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

// clang-format off: these fragments share declarations in this order.
#include "incrementalNativeObjects.inl"
#include "incrementalSourceScopes.inl"
#include "incrementalVariableBindings.inl"
#include "incrementalInferenceSymbolDependencies.inl"
#include "incrementalClassDependencies.inl"
#include "incrementalInferenceSummaryStore.inl"
#include "incrementalNativeLayout.inl"
#include "incrementalNativeSymbols.inl"
#include "incrementalNativeDependencies.inl"
#include "incrementalCompilationPartitions.inl"
#include "incrementalClassIdentityPaths.inl"
#include "incrementalConstraintValue.inl"
#include "incrementalInferenceTypes.inl"
#include "incrementalInferenceIdentities.inl"
#include "incrementalOptimizationInputs.inl"
#include "incrementalOptimization.inl"
#include "incrementalOptimizedNativeUnits.inl"
#include "incrementalInferenceValues.inl"
#include "incrementalInferenceKeys.inl"
#include "incrementalInferenceEffects.inl"
#include "incrementalInferenceParameterState.inl"
#include "incrementalClassRequests.inl"
#include "incrementalClassInstantiations.inl"
#include "incrementalInferenceClassSnapshot.inl"
#include "incrementalBodySources.inl"
#include "incrementalInferenceDiagnostics.inl"
#include "incrementalInferenceFailure.inl"
#include "incrementalValidation.inl"
#include "incrementalBodyReferences.inl"
#include "incrementalBodyWriter.inl"
#include "incrementalBodyReader.inl"
#include "incrementalInferenceReuse.inl"
#include "incrementalInferenceProducts.inl"

#include "incrementalInferenceUnitInputs.inl"
#include "incrementalInferenceUnitGraph.inl"
#include "incrementalDependencyGraph.inl"
#include "incrementalConstraintProbeReuse.inl"
#include "incrementalInferenceUnitCheck.inl"
#include "incrementalRootExecution.inl"
#include "incrementalInferenceController.inl"
#include "incrementalInferenceState.inl"
// clang-format on

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
		InferenceSummaryStore summaryStore(directory, statistics);
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
			if (const auto &state = previousFailure->at("inference_state"); !state.is_null())
				summaryStore.observe(
					state.at("inference_summary_keys"), state.at("inference_body_keys"), state.at("inference_class_keys")
				);
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
				summaryStore.observe(
					cached->at("inference_summary_keys"), cached->at("inference_body_keys"), cached->at("inference_class_keys")
				);
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
			IncrementalMatcherCache matcherCache(context, previousMatches, [&](std::string_view key) {
				return readEntry(directory / ("matcher-segment-" + std::string(key) + ".entry"));
			});
			InitialParameterCounts initialParameterCounts(
				context, previous ? &*previous : nullptr, recording->observations, statistics.parameterCountCacheMilliseconds
			);
			IncrementalClassification classification(
				context, matcherCache, previous ? &*previous : nullptr, recording->observations, initialParameterCounts,
				statistics.classification
			);
			context.selectCachedBodiesForResolutionReuse = [&]() {
				auto selectionStart = Clock::now();
				std::vector<Section *> selected = classification.select();
				statistics.cachedBodySelectionMilliseconds += elapsedMilliseconds(selectionStart);
				return selected;
			};
			context.invalidateCachedClassifications = [&]() {
				return classification.invalidated();
			};
			std::string initialMatcherTree;
			MatcherPathSnapshots matcherPaths(directory, compilerIdentity);
			InitialMatcherProjection initialMatcherProjection;
			PriorMatchKeys precedenceKeys;
			std::vector<BodyResolutionUnit> bodyUnits;
			context.reuseMatcherResult = std::bind_front(&IncrementalMatcherCache::reuse, &matcherCache);
			context.recordMatcherResult = std::bind_front(&IncrementalMatcherCache::record, &matcherCache);
			context.prepareDeferredMatcherReplay = [&](const std::list<PatternReference *> &references) {
				auto projectionStart = Clock::now();
				classification.recordOutputs();
				const Json *prior = previous && compatibleObservations(*previous, recording->observations, context)
										? &previous->at("all_function_projection")
										: nullptr;
				initialMatcherTree = matcherTreeSignature(context);
				initialMatcherProjection.capture(
					context, recording->observations, references, prior, initialParameterCounts.bodies(),
					initialParameterCounts.sections(), initialMatcherTree
				);
				statistics.matcherProjectionMilliseconds += elapsedMilliseconds(projectionStart);
				statistics.matcherDeclarationsReused = initialMatcherProjection.declarationsReused;
				matcherPaths.retain(initialMatcherTree, context);
				if (previous)
					prepareDeferredMatchReplay(
						context, *previous, recording->observations, initialMatcherTree, matcherPaths, matcherCache,
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
						context, previous->at("all_function_projection"), matcherCache, precedenceKeys, matcherPaths,
						references, statistics.precedenceTreeSignatureMilliseconds, statistics.precedenceSignatureMilliseconds,
						statistics.precedenceRuleSignatureMilliseconds, statistics.precedenceSignaturesReused
					);
			};
			statistics.cacheRestoreMilliseconds = elapsedMilliseconds(restoreStart);
			auto saveMatcher = [&]() {
				auto saveStart = Clock::now();
				auto phaseStart = saveStart;
				double phaseCPU = processCPUMilliseconds();
				auto recordPhase = [&](double &wall, double &cpu) {
					wall += elapsedMilliseconds(phaseStart);
					cpu += processCPUMilliseconds() - phaseCPU;
					phaseStart = Clock::now();
					phaseCPU = processCPUMilliseconds();
				};
				auto snapshot = matcherCache.snapshot();
				const std::string &payload = snapshot.index;
				statistics.matcherSnapshotBytes += payload.size();
				for (const auto &[key, bytes] : snapshot.segments)
					statistics.matcherSnapshotBytes += bytes.size();
				statistics.matcherStorage = matcherCache.storageStatistics();
				recordPhase(statistics.matcherSerializeMilliseconds, statistics.matcherSerializeCPUMilliseconds);
				std::string key = digest(payload);
				recordPhase(statistics.matcherDigestMilliseconds, statistics.matcherDigestCPUMilliseconds);
				for (const auto &[segmentKey, bytes] : snapshot.segments)
					writeEntry(directory / ("matcher-segment-" + segmentKey + ".entry"), bytes);
				writeEntry(directory / ("matcher-" + key + ".entry"), payload);
				recordPhase(statistics.matcherStoreMilliseconds, statistics.matcherStoreCPUMilliseconds);
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
			IncrementalSourceScopes sourceScopes(context);
			IncrementalVariableBindings bindings(
				context, matcherCache, statistics, sourceScopes, initialMatcherProjection, previous ? &*previous : nullptr,
				previousFailure ? &*previousFailure : nullptr
			);
			IncrementalValidation validation(context, statistics, sourceScopes, [&]() {
				Json records = Json::object();
				if (previous) {
					auto start = Clock::now();
					statistics.cachePayloadsLoaded += hydrateProgramField(*previous, directory, "validation_units");
					statistics.cachePayloadLoadMilliseconds += elapsedMilliseconds(start);
					records = previous->at("validation_units");
				}
				if (previousFailure)
					for (const auto &[path, record] : previousFailure->at("validation_units").items())
						records[path] = record;
				return records;
			});
			auto stageStart = Clock::now();
			bool compiled = compileBeforeTypeInference(context.options.inputPath, context, [&]() {
				return validation.run();
			});
			statistics.frontendMilliseconds = elapsedMilliseconds(stageStart);
			std::map<std::string, Section *> sections;
			std::string finalMatcherTree;
			if (compiled) {
				sections = sourceScopes.canonical();
				finalMatcherTree = matcherTreeSignature(context);
				matcherPaths.retain(finalMatcherTree, context);
			}
			std::string frontendDiagnosticText;
			std::set<std::string> frontendDiagnosticSources;
			Json frontendDiagnosticRecords =
				compiled ? captureCachedDiagnostics(context, frontendDiagnosticText, frontendDiagnosticSources) : Json::array();
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
			if (reuseTypeInference) {
				const Json &prior = previous->at("all_function_projection");
				if (initialMatcherTree == prior.at("initial_matcher_tree").get_ref<const std::string &>() &&
					finalMatcherTree == prior.at("final_matcher_tree").get_ref<const std::string &>())
					refreshedProjection = refreshInactiveFunctionProjection(
						context, recording->observations, sections, matcherCache, prior, previous->at("inactive_projection"),
						statistics.inactiveProjectionMatchesRechecked, statistics.sourceProjectionWork.matchEntriesReused
					);
			}
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
					context, recording->observations, sections, matcherCache, matcherPaths, initialMatcherTree,
					finalMatcherTree, previous ? &*previous : nullptr, &statistics.sourceProjectionWork,
					previous && compatibleObservations(*previous, recording->observations, context)
				);
			if (compiled) {
				auto projectionStart = Clock::now();
				resolvedFunctionProjection["binding_units"] = bindings.snapshot();
				resolvedFunctionProjection["initial_parameter_counts"] = initialParameterCounts.snapshot();
				resolvedFunctionProjection["matcher_projection"] =
					initialMatcherProjection.finish(resolvedFunctionProjection, matcherCache);
				resolvedFunctionProjection["matcher_projection"]["classifications"] = classification.snapshot();
				statistics.matcherProjectionMilliseconds += elapsedMilliseconds(projectionStart);
			}
			statistics.projectionMilliseconds = elapsedMilliseconds(stageStart);
			std::map<const Section *, std::string> sectionPaths;
			for (const auto &[path, section] : sections)
				sectionPaths.emplace(section, path);
			IncrementalInferenceReads inferenceReads(sectionPaths);
			IncrementalInferenceProducts products(context, sectionPaths, summaryStore, inferenceReads, statistics);
			InferenceValueIdentities valueIdentities(sectionPaths);
			// Signatures and scope rules are finalized before preinference. Their
			// source inputs stay immutable through body inference and persistence.
			std::map<std::string, std::string> inferenceInputFingerprints;
			std::unique_ptr<IncrementalIRReuse> functionReuse;
			std::unique_ptr<IncrementalInferenceReuse> inferenceReuse;
			std::unordered_set<const Section *> graphStable;
			Json *priorInference = previous ? &*previous : nullptr;
			if (previousFailure && !previousFailure->at("inference_state").is_null())
				priorInference = &previousFailure->at("inference_state");
			if (compiled && !reuseTypeInference) {
				context.recordInferenceDependency = [&](const auto &owner, const auto &dependency, const auto &scope) {
					inferenceReads.recordDependency(owner, dependency, scope);
				};
				context.recordInferenceValue = [&](const auto &owner, const auto &dependency, const auto &scope,
												   const auto &arguments, const auto &value) {
					inferenceReads.recordValue(
						owner, dependency, scope, captureInferenceValueQuery(arguments, value, valueIdentities)
					);
				};
				context.recordCallableLookup = [&](const auto &owner, const auto &source, auto signature, const auto &matches) {
					inferenceReads.record(owner, source, signature, matches);
				};
#include "incrementalInferenceSetup.inl"
			}
			stageStart = Clock::now();
			if (compiled && !reuseTypeInference)
				compiled = finishTypeInference(context);
			statistics.inferenceMilliseconds = elapsedMilliseconds(stageStart);
			statistics.reusedRootExecution = context.reuseRootExecution;
			if (compiled || context.hasCompleted(ParseContext::CompilationStage::InferredConstraints)) {
				if (reuseTypeInference) {
					const Json &inferred = previous->at("all_function_projection");
					for (const char *field : {"constraint_probe_schedule", "class_instantiations"})
						resolvedFunctionProjection[field] = inferred.at(field);
				} else {
					resolvedFunctionProjection["constraint_probe_schedule"] =
						captureConstraintProbeSchedule(context, sectionPaths, inferenceReads, priorInference);
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
			context.restoreInferredBody = {};
			context.runIncrementalPreinference = {};
			context.recordCallableLookup = {};
			context.recordInferenceDependency = {};
			context.recordInferenceValue = {};
			statistics.callableLookupsObserved = inferenceReads.observedCount();
			statistics.failedCallableLookupsObserved = inferenceReads.failedCount();
			statistics.callableLookupsChecked = inferenceReads.checkedCount();
			statistics.callableLookupsChanged = inferenceReads.changedKeys().size();
			statistics.matcherCacheHits = matcherCache.hits();
			statistics.referenceMetadataUnitsLoaded = matcherCache.referenceMetadataLoads();
			statistics.referenceMetadataUnitsReused = matcherCache.referenceMetadataReuses();
			statistics.matcherPathSnapshotsBuilt = matcherPaths.pathsBuilt();
			statistics.matcherPathSnapshotsLoaded = matcherPaths.pathsLoaded();
			statistics.deferredMatchKeysReplayed = matcherCache.deferredKeyReplays();
			statistics.precedenceMatchKeysReplayed = matcherCache.precedenceKeyReplays();
			statistics.precedenceMatcherCacheHits = matcherCache.precedenceHits();
			statistics.matcherKeyMilliseconds = matcherCache.keyTimeMilliseconds();
			statistics.matcherKeyComputations = matcherCache.keyComputations();
			statistics.matcherSignatureLookups = matcherCache.signatureLookups();
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
					auto diagnostics = renderCachedDiagnostics(*previous, *recording, context, {});
					requireCompilerInvariant(diagnostics.has_value(), "unchanged diagnostic sources lost their ranges");
					std::cerr << *diagnostics;
					context.diagnostics.clear();
					statistics.reusedDiagnosticRecords = previous->at("diagnostic_records").size();
					Json result = *previous;
					result["diagnostics"] = std::move(*diagnostics);
					result["validation_units"] = validation.snapshot();
					result["files"] = recording->observations;
					result["inactive_projection"] = std::move(inactiveProjection);
					result["all_function_projection"] = std::move(resolvedFunctionProjection);
					result["frontend_diagnostic_records"] = std::move(frontendDiagnosticRecords);
					result["frontend_diagnostic_sources"] = std::move(frontendDiagnosticSources);
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
				InferenceValueIdentities functionIdentities(sectionPaths);
				for (const auto &[section, path] : sectionPaths)
					if (hasCodegenSectionIdentity(*section))
						context.functionSymbolIdentities.emplace(section, sectionIdentityDigest(path));
				context.recordGeneratedFunction = [&](Section *section, const InstantiationKey &key, llvm::Function *function,
													  bool callable) {
					std::string name = function->getName().str();
					functionSections.emplace(name, sectionPaths.at(section));
					if (callable)
						callableFunctions[name] = true;
					functionInstantiations[name] = inferenceSummaryKey(key, functionIdentities).value_or(aggregateGraphVariant);
				};
				if (functionReuse) {
					context.reuseGeneratedFunction = [&](Section *section, Instantiation &, llvm::Function *function) {
						return functionReuse->reuse(section, function, functionInstantiations);
					};
					context.importReusedFunctions = [&](ParseContext &compiled) {
						return functionReuse->import(compiled, functionInstantiations);
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
					result["validation_units"] = validation.snapshot();
					result["frontend_diagnostic_records"] = std::move(frontendDiagnosticRecords);
					result["frontend_diagnostic_sources"] = std::move(frontendDiagnosticSources);
					if (functionReuse) {
						for (const std::string &name : functionReuse->reusedFunctionNames()) {
							functionSections.emplace(name, previous->at("function_sections").at(name).get<std::string>());
							if (previous->at("callable_functions").contains(name))
								callableFunctions[name] = true;
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
					Json globalTypes = compiled.reuseRootExecution
										   ? previous->at("global_types")
										   : captureGlobalTypeProjection(sectionPaths).value_or(Json(nullptr));
					result["global_types"] = globalTypes;
					result["inference_summaries"] =
						inferenceReuse
							? inferenceReuse->snapshot(
								  sectionPaths, globalTypes, result.at("function_sections"),
								  result.at("function_instantiations"), &statistics.reusedInferenceSummarySections
							  )
							: captureInferenceSummaries(
								  compiled, products.sourceIndex(), sectionPaths, globalTypes, summaryStore,
								  nativeInferenceUnits(result.at("function_sections"), result.at("function_instantiations")),
								  nullptr, nullptr, false, &products.references()
							  );
					result["root_diagnostics"] =
						captureRootInferenceDiagnostics(compiled, sectionPaths, products.sourceIndex());
					auto graphStart = Clock::now();
					result["section_dependency_graph"] = captureSectionDependencyGraph(
						compiled, result, sectionPaths, inferenceReads,
						result.at("all_function_projection").at("constraint_probe_schedule").at("callable_lookups"), statistics,
						inferenceInputFingerprints, priorInference ? &priorInference->at("section_dependency_graph") : nullptr,
						graphStable, retainedGraphVariants(functionReuse.get(), previous ? &*previous : nullptr), &products
					);
					statistics.dependencyGraphMilliseconds = elapsedMilliseconds(graphStart);
					retainInferenceInputPaths(
						result.at("all_function_projection").at("constraint_probe_schedule"),
						result.at("section_dependency_graph"), retainedActivePaths
					);
					result["inactive_projection"] = captureInactiveProjection(
						result.at("all_function_projection").at("declarations"), compiled, recording->observations, nullptr,
						&generatedFunctionPaths, &sections, priorInactiveSources, &statistics.sourceProjectionWork, nullptr,
						&retainedActivePaths
					);
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
				if (functionReuse)
					recordIRReuseStatistics(statistics, *functionReuse, *previous, sectionPaths);
				context.recordGeneratedFunction = {};
				context.reuseGeneratedFunction = {};
				context.importReusedFunctions = {};
			} else if (context.compilationStage >= ParseContext::CompilationStage::ImportedFiles &&
					   !context.diagnostics.empty()) {
				std::string diagnostics;
				std::set<std::string> diagnosticSources;
				Json records = captureCachedDiagnostics(context, diagnostics, diagnosticSources);
				Json savedBindings = bindings.snapshot();
				Json result{
					{"files", recording->observations},
					{"diagnostics", diagnostics},
					{"diagnostic_records", std::move(records)},
					{"diagnostic_sources", std::move(diagnosticSources)},
					{"binding_units", std::move(savedBindings)},
					{"matcher_key", saveMatcher()},
					{"parser_key", saveParser()}
				};
				recordSemanticSources(result, *recording, context);
				result["validation_units"] = validation.snapshot();
				Json state = nullptr;
				if (context.hasCompleted(ParseContext::CompilationStage::InferredConstraints)) {
					auto snapshotStart = Clock::now();
					state = inferenceStateManifest(
						directory, captureFailedInferenceState(
									   context, *recording, resolvedFunctionProjection, sectionPaths, inferenceReads,
									   inferenceReuse.get(), summaryStore, statistics, inferenceInputFingerprints,
									   priorInference, graphStable, previous ? &*previous : nullptr, products
								   )
					);
					statistics.snapshotMilliseconds += elapsedMilliseconds(snapshotStart);
				} else if (priorInference)
					state = inferenceStateManifest(directory, *priorInference);
				result["inference_state"] = std::move(state);
				writeEntry(failedEntry, result.dump());
			}
		}
		auto finalizeStart = Clock::now();
		if (success)
			fs::remove(failedEntry);
		trimCache(directory);
		statistics.cacheFinalizeMilliseconds = elapsedMilliseconds(finalizeStart);
		statistics.fileObservationRequests = recording->observationRequests();
		statistics.fileObservationsComputed = recording->observationsComputed();
		statistics.fileObservationsReused = recording->observationsReused();
		statistics.totalMilliseconds = elapsedMilliseconds(compileStart);
		if (showStatistics)
			emitIncrementalStatistics(context, statistics);
		return success;
	} catch (const std::exception &error) {
		std::cerr << "Incremental compilation failed: " << error.what() << '\n';
		return false;
	}
}
