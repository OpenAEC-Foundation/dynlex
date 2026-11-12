#pragma once
#include "diagnostic.h"
#include <optional>

struct DiagnosticExpressionSnapshot {
	Range range;
	std::string text;
};

// A terminal concrete query failure, relative to its caller's trace depth.
struct InferenceFailure {
	std::string detail;
	std::shared_ptr<const ConfiguredDiagnosticMessage> configuredDetail;
	DiagnosticExpressionSnapshot snapshot;
	std::vector<RelatedInfo> relatedInfo;
	std::optional<Diagnostic> diagnostic;
	int priority = -1;
	bool traced = false;
};
