#include "parseContext.h"
#include "matchProgress.h"
#include <iostream>

void ParseContext::printDiagnostics() {
	for (Diagnostic d : diagnostics) {
		std::cerr << d.toString() << "\n";
	}
}

PatternMatch *ParseContext::match(PatternReference *reference) {
	MatchProgress progress = MatchProgress(this, reference);
	std::vector<MatchProgress> queue = {progress};
	while (queue.size()) {
		MatchProgress &currentProgress = queue.back();
		std::vector<MatchProgress> nextSteps = currentProgress.step();
		if (currentProgress.isComplete()) {
			if (currentProgress.match.matchedEndNode && currentProgress.match.matchedEndNode->matchingDefinition) {
				auto *def = currentProgress.match.matchedEndNode->matchingDefinition;
				std::cerr << "MATCH: " << std::string(reference->pattern.text) << " -> " << std::string(def->range.subString) << " (" << def->range.toString() << ")\n";
			}
			return new PatternMatch(currentProgress.match);
		}
		queue.pop_back();
		queue.insert(queue.end(), nextSteps.begin(), nextSteps.end());
	}
	return nullptr;
}
