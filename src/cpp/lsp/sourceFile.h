#pragma once
#include <string>

namespace lsp {

// Base class for source file content storage
struct SourceFile {
	SourceFile(std::string uri, std::string content) : uri(std::move(uri)), content(std::move(content)) {}
	virtual ~SourceFile() = default;

	std::string uri;
	std::string content;
};

} // namespace lsp
