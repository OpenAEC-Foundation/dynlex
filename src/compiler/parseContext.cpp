#include "parseContext.h"
#include "matchProgress.h"
#include <iostream>

void ParseContext::reportDiagnostics() {
	for (Diagnostic d : diagnostics) {
		// report all diagnostics with cerr

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
