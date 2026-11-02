#pragma once
#include "parseContext.h"
#include <string>
#include <vector>

// Every source normalization uses the same authored-to-generated mapping.
class LogicalLineBuilder {
  public:
	LogicalLineBuilder(ParseContext &context, CodeLine &source) : context(context), source(source) {}

	void appendSource(size_t start, size_t end) {
		int transformedStart = static_cast<int>(text.size());
		text += source.fullText.substr(start, end - start);
		source.appendSourceSlices(slices, start, end, transformedStart);
	}

	void appendSynthetic(std::string_view value, size_t sourceAnchor) {
		int transformedStart = static_cast<int>(text.size());
		text += value;
		SourceLocation anchor = source.mapOffsetToSource(static_cast<int>(sourceAnchor));
		slices.push_back({transformedStart, transformedStart, anchor.sourceFile, anchor.sourceFileLineIndex, anchor.column});
	}

	CodeLine *finish(
		std::string_view indent, int logicalIndentOffset, DefinitionShorthand shorthand = DefinitionShorthand::None,
		bool synthetic = false
	) {
		CodeLine *result =
			context.createCodeLine(source.sourceFile, source.sourceFileLineIndex, std::move(text), std::move(slices));
		result->rightTrimmedText = result->fullText;
		result->logicalIndentOffset = logicalIndentOffset;
		result->hasIndentOverride = !indent.empty();
		result->indentOverride = std::string(indent);
		result->definitionShorthand = shorthand;
		result->synthetic = synthetic;
		return result;
	}

	CodeLine *finish() {
		std::string_view indent = source.hasIndentOverride ? std::string_view(source.indentOverride) : std::string_view{};
		CodeLine *result = finish(indent, source.logicalIndentOffset, source.definitionShorthand, source.synthetic);
		result->inlineBodyFollows = source.inlineBodyFollows;
		return result;
	}

  private:
	ParseContext &context;
	CodeLine &source;
	std::string text;
	std::vector<SourceSlice> slices;
};
