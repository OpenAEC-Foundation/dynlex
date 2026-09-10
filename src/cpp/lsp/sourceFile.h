#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace lsp {

// Base class for source file content storage
struct SourceFile {
	SourceFile(std::string uri, std::string content);
	virtual ~SourceFile() = default;

	std::string uri;
	std::string content;
	void setContent(std::string text);
	std::string_view getLine(int lineIndex) const;
	std::string_view getLineWithTerminator(int lineIndex) const;
	int lineCount() const { return static_cast<int>(lineOffsets.size()); }

  protected:
	std::vector<size_t> lineOffsets;
};

} // namespace lsp
