#include "parseContext.h"
#include <iostream>
#include "matchProgress.h"

void ParseContext::reportDiagnostics()
{
	for (Diagnostic d : diagnostics)
	{
		// report all diagnostics with cerr

		std::cerr << d.toString() << "\n";
	}
}

PatternMatch *ParseContext::match(PatternReference *reference)
{
	MatchProgress progress = MatchProgress(this, reference);
	std::vector<MatchProgress> queue = {progress};
	while (queue.size())
	{
		MatchProgress& currentProgress = queue.front();
		if (currentProgress.result)
		{
			return currentProgress.result;
		}
		auto nextSteps = currentProgress.step();
		queue.pop_back();
		queue.insert(queue.end(), nextSteps.begin(), nextSteps.end());
	}
	return nullptr;
}
