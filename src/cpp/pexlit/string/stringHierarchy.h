#pragma once
#include <string>
#include <vector>

struct StringHierarchy {
	char character;
	size_t start;
	size_t end{};
	std::vector<StringHierarchy *> children{};
	StringHierarchy(char character, size_t start) : character(character), start(start) {}
	~StringHierarchy();
	StringHierarchy *cloneWithOffset(size_t offset);
};