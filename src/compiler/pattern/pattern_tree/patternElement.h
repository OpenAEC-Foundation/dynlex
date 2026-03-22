#pragma once
#include "range.h"
#include <string>
#include <vector>

#include "type.h"
struct ParseContext;

struct PatternElement {
	enum Type {
		// anything not in [A-Za-z0-9]+
		// examples: ' ', '%'
		Other,
		// any string looking like a variable: [A-Za-z0-9]+
		// examples: 'the', 'or'
		VariableLike,
		// a variable
		Variable,
		// {word:name} — matches a single word, captured as a string literal (not a variable)
		Word,
		// [alternative1|alternative2|...] — each alternative is a sequence of elements
		Choice,
		Count
	};
	Type type;
	// for example: 'the'
	std::string text;
	// position relative to pattern start
	size_t startPos{};
	PatternElement(Type type, const std::string &text = {}, size_t startPos = {})
		: type(type), text(text), startPos(startPos) {}
};

// Extended pattern element used in pattern definitions. Adds Choice alternatives and
// typed argument constraints ({type:name} syntax). PatternTreeNode derives from the
// base PatternElement and does not inherit these fields.
struct DefinitionPatternElement : public PatternElement {
	// for Choice type: each alternative is a sequence of elements
	std::vector<std::vector<DefinitionPatternElement>> alternatives;
	// for Variable type: type constraint name from {type:name} syntax (empty if unconstrained)
	std::string typeConstraintName;
	// resolved type constraint (set during type constraint resolution step, Undeduced if unconstrained)
	DataType resolvedTypeConstraint;
	// true when a plain VariableLike word was implicitly promoted into a parameter by body usage
	bool promotedFromVariableLike = false;
	// the first body reference range that caused the implicit promotion
	Range firstImplicitPromotionUseRange;

	using PatternElement::PatternElement;
	// Construct from a base PatternElement (for converting getPatternElements results)
	DefinitionPatternElement(const PatternElement &base) : PatternElement(base) {}
};

// Parse plain text (no brackets) into pattern elements
std::vector<PatternElement> getPatternElements(std::string_view patternString);

// Parse pattern text with [bracket|alternatives] and {type:name} captures into definition elements.
// Returns false and emits a diagnostic on the first parse error.
bool parsePatternElements(
	ParseContext &context, Range patternRange, std::string_view patternString, std::vector<DefinitionPatternElement> &elements,
	size_t offset = 0
);

// Visit all leaf (non-Choice) elements recursively, including inside Choice alternatives
template <typename Elem, typename F> void forEachLeafElement(std::vector<Elem> &elements, F &&callback) {
	for (auto &element : elements) {
		if (element.type == PatternElement::Type::Choice) {
			for (auto &alternative : element.alternatives) {
				forEachLeafElement(alternative, callback);
			}
		} else {
			callback(element);
		}
	}
}
