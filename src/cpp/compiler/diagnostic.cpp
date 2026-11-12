#include "diagnostic.h"
#include "codeLine.h"
#include "compilerUtils.h"
#include "parseContext.h"
#include <algorithm>

bool Diagnostic::sameEmission(const Diagnostic &other) const {
	if (!emission || !other.emission)
		return false;
	if (emission->publication && other.emission->publication) {
		if (emission->publication == other.emission->publication)
			return true;
		if (!emission->publication->identity.empty() &&
			emission->publication->identity == other.emission->publication->identity)
			return true;
	}
	return !emission->discriminator.empty() && emission->discriminator == other.emission->discriminator &&
		   range.line == other.range.line && range.start() == other.range.start() && range.end() == other.range.end();
}

Diagnostic Diagnostic::relativeToInference(int base) const {
	Diagnostic relative = *this;
	std::erase_if(relative.relatedInfo, [&](const RelatedInfo &note) {
		return note.inferenceDepth >= 0 && note.inferenceDepth < base;
	});
	for (RelatedInfo &note : relative.relatedInfo)
		if (note.inferenceDepth >= 0) {
			note.inferenceDepth -= base;
			note.inferenceOwnerBase -= base;
		}
	if (auto &metadata = relative.editEmission(); metadata.traceBase >= 0)
		metadata.traceBase -= base;
	return relative;
}

ConfiguredDiagnosticMessage configuredDiagnosticMessage(
	const ParseContext &, Range sourceRange, std::string_view key, std::string_view variant,
	std::vector<std::pair<std::string, std::string>> placeholders
) {
	SourceLocation start = sourceRange.sourceStart();
	return {std::string(key),
			std::string(variant),
			std::move(placeholders),
			start.sourceFile && start.sourceFile->isInternal,
			{},
			{}};
}

std::string renderConfiguredDiagnosticMessage(const ParseContext &context, const ConfiguredDiagnosticMessage &message) {
	const SyntaxConfig &syntax = message.internal ? context.builtinSyntax : context.projectSyntax;
	std::vector<std::pair<std::string, std::string>> placeholders = message.placeholders;
	for (const ConfiguredDiagnosticMessage &nested : message.nestedPlaceholders) {
		std::string rendered = renderConfiguredDiagnosticMessage(context, nested);
		auto found = std::find_if(placeholders.begin(), placeholders.end(), [&](const auto &entry) {
			return entry.first == nested.placeholderName;
		});
		requireCompilerInvariant(found != placeholders.end(), "configured diagnostic placeholder is missing");
		found->second = std::move(rendered);
	}
	return renderConfiguredMessage(syntax, message.key, message.variant, placeholders);
}

RelatedInfo configuredRelatedInfo(
	const ParseContext &context, Range syntaxRange, std::string_view key, std::string_view variant, Range relatedRange,
	std::vector<std::pair<std::string, std::string>> placeholders
) {
	ConfiguredDiagnosticMessage configured =
		configuredDiagnosticMessage(context, syntaxRange, key, variant, std::move(placeholders));
	return {
		renderConfiguredDiagnosticMessage(context, configured), relatedRange,
		std::make_shared<const ConfiguredDiagnosticMessage>(std::move(configured))
	};
}

Diagnostic unknownTypeConstraintDiagnostic(const ParseContext &context, Range range, std::string_view constraint) {
	return Diagnostic(context, Diagnostic::Level::Error, "unknown type constraint", range, "type_constraint", constraint);
}

Diagnostic Diagnostic::configParseError(std::string_view diagnosticMessage, Range diagnosticRange) {
	Diagnostic diagnostic;
	diagnostic.level = Diagnostic::Level::Error;
	diagnostic.message = std::string(diagnosticMessage);
	diagnostic.range = diagnosticRange;
	return diagnostic;
}

std::string Diagnostic::toString() const {
	std::string result = range.toString() + ": " + enumToString(level) + ": " + message;
	for (const auto &related : relatedInfo)
		result += "\n  note: " + related.message + " " + related.range.toString();
	return result;
}

void Diagnostic::setConfiguredMessage(
	const ParseContext &context, Range syntaxRange, std::string_view key, std::string_view variant,
	std::vector<std::pair<std::string, std::string>> placeholders
) {
	configuredMessage = std::make_shared<const ConfiguredDiagnosticMessage>(
		configuredDiagnosticMessage(context, syntaxRange, key, variant, std::move(placeholders))
	);
	message = renderConfiguredDiagnosticMessage(context, *configuredMessage);
}

void Diagnostic::setConfiguredPlaceholder(
	const ParseContext &context, std::string name, ConfiguredDiagnosticMessage replacement
) {
	requireCompilerInvariant(configuredMessage != nullptr, "configured placeholder requires a configured diagnostic");
	replacement.placeholderName = std::move(name);
	ConfiguredDiagnosticMessage updated = *configuredMessage;
	updated.nestedPlaceholders.push_back(std::move(replacement));
	configuredMessage = std::make_shared<const ConfiguredDiagnosticMessage>(std::move(updated));
	message = renderConfiguredDiagnosticMessage(context, *configuredMessage);
}
