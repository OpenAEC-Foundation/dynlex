#include "globalsSection.h"
#include "parseContext.h"

void GlobalsSection::addItem(ParseContext &context, std::string_view varName, CodeLine * /*line*/) {
	// Add to parent Expression/Effect section's globalVariables list
	std::string varNameStr(varName);
	parent->globalVariables.push_back(varNameStr);
	context.declaredGlobalVariables.insert(varNameStr);
}
