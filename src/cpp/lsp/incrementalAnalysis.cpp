#include "compiler.h"
#include "lspAnalysis.h"
#include "lspFileSystem.h"
#include "pathUtils.h"
#include "recordingFileSystem.h"

namespace lsp {
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
			files.matches(previous->sources)) {
			prior.builtinSyntax.messages = std::move(configuration->builtinSyntax.messages);
			prior.projectSyntax.messages = std::move(configuration->projectSyntax.messages);
			prior.builtinSyntaxConfigPath = std::move(configuration->builtinSyntaxConfigPath);
			prior.projectSyntaxConfigPath = std::move(configuration->projectSyntaxConfigPath);
			refreshDiagnostics(prior);
			return {previous->context, std::move(files.observations), std::move(files.configurationObservations), true, true};
		}
	}
	auto context = createContext(options, documents);
	compile(uri, *context);
	auto &files = static_cast<RecordingFileSystem &>(*context->fileSystem);
	return {std::move(context), std::move(files.observations), std::move(files.configurationObservations), true, false};
}

void addAnalysisTrackingForMain(ImportGraph &graph, const std::string &mainUri, const LspAnalysisResult &result) {
	graph[mainUri].insert(mainUri);
	for (const Json *observations : {&result.sources, &result.configurations})
		for (const auto &[path, _] : observations->items())
			graph[pathutil::toAbsoluteUri(path)].insert(mainUri);
}
} // namespace lsp
