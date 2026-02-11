#pragma once
#include <string>

struct VariableReference;
struct VariableMatch {
	std::string name;
	size_t lineStartPos;
	size_t lineEndPos;
	// Set when the VariableReference is created from this match
	VariableReference *variableReference{};
};

struct WordMatch {
	std::string text;
	size_t lineStartPos;
	size_t lineEndPos;
};
