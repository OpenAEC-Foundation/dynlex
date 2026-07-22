#include "alignmentSection.h"
#include "classSection.h"
#include "diagnostic.h"
#include "parseContext.h"

bool AlignmentSection::applyValue(ParseContext &context, CodeLine *line, int value) {
	if (!validateByteAlignment(context, line, value))
		return false;
	ClassDefinition *definition = static_cast<ClassSection *>(parent)->classDefinition;
	if (definition->alignment != 0) {
		context.addDiagnostic(Diagnostic(
			context, Diagnostic::Level::Error, "class has more than one alignment directive", Range(line, line->patternText)
		));
		return false;
	}
	definition->alignment = static_cast<unsigned>(value);
	definition->alignmentRange = Range(line, line->patternText);
	return true;
}
