#include "listingSection.h"
#include "codeLine.h"
#include "diagnostic.h"
#include "parseContext.h"
#include "parseUtils.h"

bool ListingSection::processLine(ParseContext &context, CodeLine *line) {
	parseCommaSeparatedListWithRanges(line->patternText, [&](std::string_view /*item*/, size_t start, size_t end) {
		addItem(context, Range(line, line->patternText.substr(start, end - start)));
	}, [&](std::string_view /*separator*/, size_t start, size_t end) {
		addSeparator(context, Range(line, line->patternText.substr(start, end - start)));
	});
	line->resolved = true;
	return true;
}

Section *ListingSection::createSection(ParseContext &context, CodeLine *line) {
	context.addDiagnostic(
		Diagnostic(context, Diagnostic::Level::Error, "listing section cannot create sections", Range(line, line->patternText))
	);
	return nullptr;
}
