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

std::vector<int> SemanticTokenBuilder::build() {
	std::vector<int> data;

	int prevLine = 0;
	int prevChar = 0;

	for (int line = 0; line < static_cast<int>(tokensByLine.size()); ++line) {
		auto &lineTokens = tokensByLine[line];

		// Sort tokens on this line by start position
		std::sort(lineTokens.begin(), lineTokens.end(), [](const SemanticToken &a, const SemanticToken &b) {
			return a.start < b.start;
		});

		for (const SemanticToken &t : lineTokens) {
			int deltaLine = line - prevLine;
			int deltaChar = (deltaLine == 0) ? (t.start - prevChar) : t.start;

			data.push_back(deltaLine);
			data.push_back(deltaChar);
			data.push_back(t.end - t.start);
			data.push_back(static_cast<int>(t.type));
			data.push_back(t.modifiers);

			prevLine = line;
			prevChar = t.start;
		}
	}

	return data;
}

} // namespace lsp
