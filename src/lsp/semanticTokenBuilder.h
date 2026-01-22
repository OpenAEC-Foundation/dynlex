#pragma once
#include "semanticTokens.h"
#include <vector>

namespace lsp {

struct SemanticToken {
	int start;
	int end;
	SemanticTokenType type;
	int modifiers = 0;
};

class SemanticTokenBuilder {
  public:
	SemanticTokenBuilder(int lineCount);
	void add(int line, SemanticToken token);
	std::vector<int> build();

  private:
	std::vector<std::vector<SemanticToken>> tokensByLine;
};

} // namespace lsp
