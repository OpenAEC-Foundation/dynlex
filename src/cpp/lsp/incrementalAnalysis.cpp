#include "compiler.h"
#include "compilerUtils.h"
#include "incrementalMatcherCache.h"
#include "lspAnalysis.h"
#include "lspFileSystem.h"
#include "pathUtils.h"
#include "recordingFileSystem.h"
#include "sourceSyntax.h"
#include "llvm/ADT/ScopeExit.h"

namespace lsp {
struct LspMatcherProducts {
	std::string index;
	std::unordered_map<std::string, std::shared_ptr<const std::string>> segments;
	bool complete = false;
	std::shared_ptr<const LspMatcherProducts> lastComplete;
};

namespace {
std::shared_ptr<ParseContext>
createContext(const ParseContext::Options &options, std::unordered_map<std::string, std::unique_ptr<TextDocument>> &documents) {
	auto context = std::make_shared<ParseContext>();
	context->options = options;
	context->fileSystem = std::make_unique<RecordingFileSystem>(std::make_unique<LspFileSystem>(documents));
	return context;
}

void refreshDiagnostics(ParseContext &context) {
	for (auto &diagnostic : context.diagnostics) {
		if (diagnostic.configuredMessage)
			diagnostic.message = renderConfiguredDiagnosticMessage(context, *diagnostic.configuredMessage);
		for (auto &note : diagnostic.relatedInfo)
			if (note.configuredMessage)
				note.message = renderConfiguredDiagnosticMessage(context, *note.configuredMessage);
		for (auto &fix : diagnostic.quickFixes)
			if (fix.configuredTitle)
				fix.title = renderConfiguredDiagnosticMessage(context, *fix.configuredTitle);
	}
}

bool sameProgramSources(ParseContext &prior, RecordingFileSystem &files, const Json &previous) {
	for (const auto &[path, observation] : previous.items()) {
		const Json &current = files.observe(path);
		if (current == observation)
			continue;
		if (!prior.hasCompleted(ParseContext::CompilationStage::InferredTypes) || current.is_null() || observation.is_null() ||
			current.at("canonical") != observation.at("canonical") || current.at("internal") != observation.at("internal"))
			return false;
		const SourceFile *oldFile = prior.importedFiles.at(observation.at("canonical").get<std::string>());
		const SourceFile *newFile = files.cachedFile(path);
		requireCompilerInvariant(oldFile && newFile, "recorded editor source has no snapshot");
		if (oldFile->lineCount() != newFile->lineCount())
			return false;
		const std::string &prefix = syntaxConfigForSourceFile(prior, oldFile).commentPrefix;
		for (int line = 0; line < oldFile->lineCount(); line++) {
			auto oldText = oldFile->getLine(line), newText = newFile->getLine(line);
			if (invalidSourceCharacterOffset(newText, prefix) ||
				sourceCodeWithoutComment(oldText, prefix) != sourceCodeWithoutComment(newText, prefix))
				return false;
		}
	}
	return true;
}

void compileWithMatcher(const std::string &uri, LspAnalysisResult &result, const LspAnalysisResult *previous) {
	const LspMatcherProducts *saved = previous && previous->profileCurrent ? previous->matcherProducts.get() : nullptr;
	ParseContext &context = *result.context;
	IncrementalMatcherCache matcher(context, saved ? saved->index : "", [&](std::string_view key) {
		requireCompilerInvariant(saved != nullptr, "editor matcher requested a segment without prior products");
		return std::optional<std::string>(*saved->segments.at(std::string(key)));
	});
	std::unique_ptr<IncrementalMatcherCache> completeMatcher;
	if (saved && saved->lastComplete)
		completeMatcher =
			std::make_unique<IncrementalMatcherCache>(context, saved->lastComplete->index, [&](std::string_view key) {
			return std::optional<std::string>(*saved->lastComplete->segments.at(std::string(key)));
		});
	context.reuseMatcherResult = [&](PatternReference *reference, MatchOptions options, MatchDependencies *dependencies) {
		auto result = matcher.reuse(reference, options, dependencies);
		if (result || !completeMatcher)
			return result;
		result = completeMatcher->reuse(reference, options, dependencies);
		if (result)
			matcher.record(reference, options, *result, dependencies);
		return result;
	};
	context.recordMatcherResult = [&](PatternReference *reference, MatchOptions options, const PatternMatch *match,
									  const MatchDependencies *dependencies) {
		result.matcherComputations++;
		matcher.record(reference, options, match, dependencies);
	};
	auto detach = llvm::scope_exit([&]() {
		context.reuseMatcherResult = {};
		context.recordMatcherResult = {};
	});
	bool complete = compile(uri, context);
	// Keep only queries used by this analysis. A failed analysis also retains
	// the last complete product set, without chaining historical failed lookups.
	auto snapshot = matcher.snapshot(IncrementalMatcherCache::SnapshotScope::Complete);
	auto products = std::make_shared<LspMatcherProducts>();
	products->complete = complete;
	if (!complete && saved)
		products->lastComplete = saved->complete ? previous->matcherProducts : saved->lastComplete;
	products->index = std::move(snapshot.index);
	std::unordered_map<std::string, std::shared_ptr<const std::string>> generated;
	for (auto &[key, bytes] : snapshot.segments)
		generated.emplace(std::move(key), std::make_shared<const std::string>(std::move(bytes)));
	Json index = Json::parse(products->index);
	for (const auto &[_, descriptor] : index.items()) {
		const auto &key = descriptor.at("payload").get_ref<const std::string &>();
		auto current = generated.find(key);
		if (current != generated.end())
			products->segments.emplace(key, std::move(current->second));
		else {
			requireCompilerInvariant(saved != nullptr, "editor matcher retained a segment without prior products");
			products->segments.emplace(key, saved->segments.at(key));
		}
	}
	result.matcherHits = matcher.hits() + (completeMatcher ? completeMatcher->hits() : 0);
	std::unordered_set<const std::string *> counted;
	auto account = [&](const LspMatcherProducts &set) {
		result.matcherBytes += set.index.size();
		for (const auto &[_, bytes] : set.segments)
			if (counted.insert(bytes.get()).second)
				result.matcherBytes += bytes->size();
	};
	account(*products);
	if (products->lastComplete)
		account(*products->lastComplete);
	result.matcherProducts = std::move(products);
}
} // namespace

LspAnalysisResult analyzeLspDocument(
	const std::string &uri, const ParseContext::Options &options,
	std::unordered_map<std::string, std::unique_ptr<TextDocument>> &documents, const LspAnalysisResult *previous
) {
	if (previous && previous->profileCurrent &&
		previous->context->hasCompleted(ParseContext::CompilationStage::ImportedFiles)) {
		auto configuration = createContext(options, documents);
		auto &files = static_cast<RecordingFileSystem &>(*configuration->fileSystem);
		ParseContext &prior = *previous->context;
		if (initializeSyntaxConfigs(*configuration, uri) &&
			prior.builtinSyntax.nonMessageIdentity == configuration->builtinSyntax.nonMessageIdentity &&
			prior.projectSyntax.nonMessageIdentity == configuration->projectSyntax.nonMessageIdentity &&
			sameProgramSources(prior, files, previous->sources)) {
			prior.builtinSyntax.messages = std::move(configuration->builtinSyntax.messages);
			prior.projectSyntax.messages = std::move(configuration->projectSyntax.messages);
			prior.builtinSyntaxConfigPath = std::move(configuration->builtinSyntaxConfigPath);
			prior.projectSyntaxConfigPath = std::move(configuration->projectSyntaxConfigPath);
			refreshDiagnostics(prior);
			LspAnalysisResult result{
				previous->context, std::move(files.observations), std::move(files.configurationObservations), true, true
			};
			result.matcherProducts = previous->matcherProducts;
			result.matcherBytes = previous->matcherBytes;
			return result;
		}
	}
	auto context = createContext(options, documents);
	LspAnalysisResult result;
	result.context = context;
	compileWithMatcher(uri, result, previous);
	auto &files = static_cast<RecordingFileSystem &>(*context->fileSystem);
	result.sources = std::move(files.observations);
	result.configurations = std::move(files.configurationObservations);
	return result;
}

void addAnalysisTrackingForMain(ImportGraph &graph, const std::string &mainUri, const LspAnalysisResult &result) {
	graph[mainUri].insert(mainUri);
	for (const Json *observations : {&result.sources, &result.configurations})
		for (const auto &[path, _] : observations->items())
			graph[pathutil::toAbsoluteUri(path)].insert(mainUri);
}
} // namespace lsp
