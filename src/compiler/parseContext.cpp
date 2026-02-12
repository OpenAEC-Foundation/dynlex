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
			return new PatternMatch(currentProgress.match);
		}
		queue.pop_back();
		queue.insert(queue.end(), nextSteps.begin(), nextSteps.end());
	}
	return nullptr;
}
