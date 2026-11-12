#pragma once
#include "compileTimeInfo.h"
#include <optional>
#include <string>
#include <tuple>
#include <vector>

struct ClassInstantiationRequest {
	std::vector<std::tuple<std::string, DataType, CompileTimeValue>> parameters;
	std::optional<std::vector<DataType>> constructionArgumentTypes;
	int index = -1;
};

std::string classInstantiationRequestKey(const ClassInstantiationRequest &request);
