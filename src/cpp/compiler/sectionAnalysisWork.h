#pragma once
#include <chrono>
#include <cstddef>

struct SectionAnalysisWork {
	size_t headersEvaluated = 0, bodyLinesDispatched = 0, finalizations = 0, localityPasses = 0;
	size_t outlineNodes = 0, outlinePatternReferences = 0, outlineVariables = 0;
	double headerMilliseconds = 0, bodyDispatchMilliseconds = 0, finalizeMilliseconds = 0, localityMilliseconds = 0;
	// Nested within header/line dispatch; do not add this to the disjoint stage operations.
	double outlineRestoreMilliseconds = 0;
};

// A null destination explicitly disables measurement on ordinary builds.
class SectionAnalysisTimer {
  public:
	explicit SectionAnalysisTimer(double *destination)
		: destination(destination), started(destination ? Clock::now() : Clock::time_point{}) {}
	~SectionAnalysisTimer() {
		if (destination)
			*destination += std::chrono::duration<double, std::milli>(Clock::now() - started).count();
	}

  private:
	using Clock = std::chrono::steady_clock;
	double *destination;
	Clock::time_point started;
};
