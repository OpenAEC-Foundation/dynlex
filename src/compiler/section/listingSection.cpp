#include "listingSection.h"
#include "codeLine.h"
#include "diagnostic.h"
#include "parseContext.h"
#include "parseUtils.h"

bool ListingSection::processLine(ParseContext &context, CodeLine *line) {
	parseCommaSeparatedList(line->patternText, [&](std::string_view item) {
		addItem(context, item, line);
	});
	line->resolved = true;
	return true;
}

Section *ListingSection::createSection(ParseContext &context, CodeLine *line) {
	context.diagnostics.push_back(
		Diagnostic(Diagnostic::Level::Error, "you can't create sections in a listing section", Range(line, line->patternText))
	);
	return nullptr;
}
