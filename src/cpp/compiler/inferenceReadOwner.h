#pragma once

#include <cstddef>
#include <memory>
#include <optional>

struct Section;
struct VariableStorageScope;

// A storage scope survives instantiation-key refinement and trial rollback.
// Ancestor trial observations also feed the section-level dependency view.
struct InferenceReadOwner {
	const Section *section{};
	std::shared_ptr<const VariableStorageScope> scope;
	std::optional<size_t> constraintProbe;
	bool direct = true;
};
