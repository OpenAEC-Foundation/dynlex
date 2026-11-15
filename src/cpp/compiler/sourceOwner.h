#pragma once
#include <cstddef>
#include <vector>

struct Section;
struct CodeLine;
struct PatternReference;
struct BodySyntaxProduct;

struct RetainedSourceOwner {
	std::vector<BodySyntaxProduct *> products;
	size_t referenceCount;
};

// Logical source lines exclude blanks/comments and use current transformed ranges.
std::vector<CodeLine *> sourceOwnerLines(const Section &owner);
std::vector<PatternReference *> sourceOwnerReferences(const Section &owner);
