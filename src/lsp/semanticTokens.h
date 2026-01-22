#pragma once
#include <string>
#include <vector>

namespace lsp {

// 3BX-specific semantic token types
// These indices must match the legend sent to the client
enum class SemanticTokenType {
	Expression = 0,
	Effect = 1,
	Section = 2,
	Variable = 3,
	Comment = 4,
	PatternDefinition = 5,
	Count
};

// Get the token type names for the legend
inline std::vector<std::string> getSemanticTokenTypes() {
	return {"expression", "effect", "section", "variable", "comment", "patternDefinition"};
}

// 3BX-specific semantic token modifiers
enum class SemanticTokenModifier { Definition = 0, Count };

// Get the token modifier names for the legend
inline std::vector<std::string> getSemanticTokenModifiers() { return {"definition"}; }

} // namespace lsp
