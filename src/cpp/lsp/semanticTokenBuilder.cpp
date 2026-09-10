#include "semanticTokenBuilder.h"
#include <algorithm>

namespace lsp {

SemanticTokenBuilder::SemanticTokenBuilder(int lineCount) : tokensByLine(lineCount) {}

void SemanticTokenBuilder::add(int line, SemanticToken token) {
	if (token.start >= token.end)
		return;

	auto &lineTokens = tokensByLine[line];

	// Collect occupied intervals on this line that overlap with token
	std::vector<std::pair<int, int>> occupied;
	for (const SemanticToken &t : lineTokens) {
		if (t.start < token.end && t.end > token.start) {
			occupied.push_back({t.start, t.end});
		}
	}

	if (occupied.empty()) {
		lineTokens.push_back(token);
		return;
	}

	// Sort occupied intervals
	std::sort(occupied.begin(), occupied.end());

	// Add slices around occupied intervals
	int pos = token.start;
	for (const auto &[occStart, occEnd] : occupied) {
		if (pos < occStart) {
			lineTokens.push_back({pos, occStart, token.type, token.modifiers});
		}
		pos = std::max(pos, occEnd);
	}

	// Add remaining slice after all occupied intervals
	if (pos < token.end) {
		lineTokens.push_back({pos, token.end, token.type, token.modifiers});
	}
}

const std::vector<std::vector<SemanticToken>> &SemanticTokenBuilder::tokenLines() const { return tokensByLine; }

} // namespace lsp
