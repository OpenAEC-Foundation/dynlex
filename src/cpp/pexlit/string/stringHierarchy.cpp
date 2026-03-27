#include "stringHierarchy.h"
#include <ranges>

StringHierarchy::~StringHierarchy() {
	for (auto child : children) {
		delete child;
	}
}

StringHierarchy *StringHierarchy::cloneWithOffset(size_t offset) {

	StringHierarchy *clone = new StringHierarchy(character, start + offset);
	clone->end = end + offset;
	clone->children.resize(children.size());
	for (auto [source, dest] : std::views::zip(children, clone->children)) {
		dest = source->cloneWithOffset(offset);
	}
	return clone;
}
