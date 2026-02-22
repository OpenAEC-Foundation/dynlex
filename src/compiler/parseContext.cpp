#include "parseContext.h"
#include "matchProgress.h"
#include <iostream>

void ParseContext::printDiagnostics() {
	for (Diagnostic d : diagnostics) {
		std::cerr << d.toString() << "\n";
	}
}

// Infer the return type of a match from its matched section's instantiations or macro body.
// Returns the return type if known, or Any if unknown/undeduced.
static DataType inferMatchReturnType(const PatternMatch &match) {
	if (!match.matchedEndNode)
		return {DataType::Kind::Any};
	for (auto *def : match.matchedEndNode->matchingDefinitions) {
		if (!def->section)
			continue;
		if (def->section->isMacro) {
			for (Section *child : def->section->children) {
				for (CodeLine *line : child->codeLines) {
					if (line->expression && line->expression->type.isDeduced())
						return line->expression->type;
				}
			}
		} else {
			for (auto &[argTypes, inst] : def->section->instantiations) {
				if (inst.returnType.isDeduced())
					return inst.returnType;
			}
		}
	}
	return {DataType::Kind::Any};
}

// Check if a match's subexpression types are valid.
// Submatches are in value context — a deduced Void return type is invalid.
// Undeduced types pass through (safe to call before inference).
bool matchTypesValid(const PatternMatch &match) {
	for (const PatternMatch &sub : match.subMatches) {
		if (!matchTypesValid(sub))
			return false;
		DataType subType = inferMatchReturnType(sub);
		if (subType.isDeduced() && subType.kind == DataType::Kind::Void)
			return false;
	}
	return true;
}

PatternMatch *ParseContext::match(PatternReference *reference) {
	MatchProgress progress = MatchProgress(this, reference);
	std::vector<MatchProgress> queue = {progress};
	while (queue.size()) {
		MatchProgress &currentProgress = queue.back();
		std::vector<MatchProgress> nextSteps = currentProgress.step();
		if (currentProgress.isComplete()) {
			if (matchTypesValid(currentProgress.match)) {
				return new PatternMatch(currentProgress.match);
			}
		}
		queue.pop_back();
		queue.insert(queue.end(), nextSteps.begin(), nextSteps.end());
	}
	return nullptr;
}
