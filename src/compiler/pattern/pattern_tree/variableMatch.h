#pragma once
#include <string>

struct VariableMatch
{
	std::string name;
	size_t lineStartPos;
	size_t lineEndPos;
};
