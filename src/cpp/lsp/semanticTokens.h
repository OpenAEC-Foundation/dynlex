#pragma once
#include "callDepthColors.h"
#include <string>
#include <vector>

namespace lsp {

// DynLex-specific semantic token types
// These indices must match the legend sent to the client
enum class SemanticTokenType {
	Function = 0,
	Section = 1,
	Variable = 2,
	Comment = 3,
	PatternDefinition = 4,
	Number = 5,
	String = 6,
	Intrinsic = 7,
	Type = 8,
	Keyword = 9,
	Count
};

// Get the token type names for the legend
inline std::vector<std::string> getSemanticTokenTypes() {
	return {"function", "section", "variable",	"comment", "patternDefinition",
			"number",	"string",  "intrinsic", "type",	   "keyword"};
}

// DynLex-specific semantic token modifiers
enum class SemanticTokenModifier {
	Definition = 0,
	Constant = 1,
	CallDepthFirst = 2,
	Count = CallDepthFirst + callDepthColorCount
};

// Get the token modifier names for the legend
inline std::vector<std::string> getSemanticTokenModifiers() {
	std::vector<std::string> modifiers{"definition", "constant"};
	for (int depth = 0; depth < callDepthColorCount; ++depth)
		modifiers.push_back("callDepth" + std::to_string(depth));
	return modifiers;
}

inline int callDepthModifier(int depth) {
	return 1 << (static_cast<int>(SemanticTokenModifier::CallDepthFirst) + depth % callDepthColorCount);
}

} // namespace lsp
