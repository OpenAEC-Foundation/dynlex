#pragma once
#include "compileTimeValue.h"

// Returns the selected IEEE value in a double container, rounding directly to
// the destination precision rather than through an intermediate float format.
std::optional<double> getCompileTimeFloatingValue(const CompileTimeValue &value, unsigned bitWidth);
