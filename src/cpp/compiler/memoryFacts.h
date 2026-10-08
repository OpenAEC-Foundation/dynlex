#pragma once

#include <memory>

struct Diagnostic;

// A proof recorded while inferring an expression is consumed according to how
// its caller uses it. Merely asking for a type consumes neither runtime form.
struct MemoryFacts {
	std::shared_ptr<const Diagnostic> value;
	std::shared_ptr<const Diagnostic> address;
	std::shared_ptr<const Diagnostic> effects;
};
