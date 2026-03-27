#include "codeLine.h"
#include "pathUtils.h"
#include "section.h"
#include "sourceFile.h"
#include <algorithm>

static bool uriMatches(const std::string &storedUri, const std::string &queryUri) {
	if (storedUri == queryUri || ("file://" + storedUri) == queryUri)
		return true;
	return pathutil::toAbsoluteUri(storedUri) == pathutil::toAbsoluteUri(queryUri);
}

bool CodeLine::isPatternDefinition() const { return sectionOpening && sectionOpening->type != SectionType::Custom; }

bool CodeLine::isPatternReference() const {
	return patternText.length() && (!sectionOpening || sectionOpening->type == SectionType::Custom);
}

void CodeLine::setOwnedText(std::string text) {
	ownedText = std::move(text);
	fullText = ownedText;
}

SourceLocation CodeLine::mapOffsetToSource(int offset) const {
	if (offset < 0)
		offset = 0;
	if (offset > static_cast<int>(fullText.size()))
		offset = static_cast<int>(fullText.size());
	if (sourceSlices.empty())
		return {sourceFile, sourceFileLineIndex, offset};

	for (const SourceSlice &slice : sourceSlices) {
		if (offset < slice.transformedStart)
			return {slice.sourceFile, slice.sourceFileLineIndex, slice.sourceColumnStart};
		if (offset <= slice.transformedEnd) {
			int localOffset = std::clamp(offset - slice.transformedStart, 0, slice.transformedEnd - slice.transformedStart);
			return {slice.sourceFile, slice.sourceFileLineIndex, slice.sourceColumnStart + localOffset};
		}
	}

	const SourceSlice &last = sourceSlices.back();
	return {last.sourceFile, last.sourceFileLineIndex, last.sourceColumnStart + (last.transformedEnd - last.transformedStart)};
}

int CodeLine::mapSourceToOffset(const std::string &uri, int lineIndex, int column) const {
	for (const SourceSlice &slice : sourceSlices) {
		if (!slice.sourceFile || !uriMatches(slice.sourceFile->uri, uri) || slice.sourceFileLineIndex != lineIndex)
			continue;
		int sliceStart = slice.sourceColumnStart;
		int sliceEnd = slice.sourceColumnStart + (slice.transformedEnd - slice.transformedStart);
		if (column < sliceStart || column > sliceEnd)
			continue;
		return slice.transformedStart + (column - sliceStart);
	}
	if (sourceSlices.empty() && sourceFile && uriMatches(sourceFile->uri, uri) && sourceFileLineIndex == lineIndex &&
		column >= 0 && column <= static_cast<int>(fullText.size())) {
		return column;
	}
	return -1;
}

bool CodeLine::containsSourceLocation(const std::string &uri, int lineIndex, int column) const {
	return mapSourceToOffset(uri, lineIndex, column) >= 0;
}
