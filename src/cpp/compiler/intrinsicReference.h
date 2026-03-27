#pragma once
#include "codegenValue.h"
#include "range.h"
#include <string>
#include <vector>

struct IntrinsicReference {
	std::string name;
	std::vector<CodegenValue> arguments;
	Range range;
};
