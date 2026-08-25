#include "pathUtils.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/LSP/Protocol.h"
#include <filesystem>
#include <stdexcept>
#include <utility>

namespace pathutil {

namespace {

llvm::StringRef asStringRef(std::string_view value) { return {value.data(), value.size()}; }

llvm::lsp::URIForFile parseFileUri(std::string_view uri) {
	llvm::Expected<llvm::lsp::URIForFile> parsed = llvm::lsp::URIForFile::fromURI(asStringRef(uri));
	if (!parsed)
		throw std::invalid_argument("invalid file URI: " + llvm::toString(parsed.takeError()));
	return std::move(*parsed);
}

} // namespace

std::string toFilesystemPath(std::string_view uriOrPath) {
	if (!isFileUri(uriOrPath))
		return std::string(uriOrPath);
	return parseFileUri(uriOrPath).file().str();
}

std::string toAbsoluteUri(std::string_view uriOrPath) {
	if (isFileUri(uriOrPath))
		return parseFileUri(uriOrPath).uri().str();

	const std::filesystem::path absolutePath = std::filesystem::absolute(std::filesystem::path(uriOrPath));
	llvm::Expected<llvm::lsp::URIForFile> uri = llvm::lsp::URIForFile::fromFile(absolutePath.string());
	if (!uri)
		throw std::invalid_argument("invalid file path: " + llvm::toString(uri.takeError()));
	return uri->uri().str();
}

std::string toDisplayPath(std::string_view uriOrPath) {
	std::filesystem::path filesystemPath = std::filesystem::absolute(toFilesystemPath(uriOrPath)).lexically_normal();
	std::error_code error;
	std::filesystem::path currentPath = std::filesystem::current_path(error);
	if (!error) {
		std::filesystem::path relativePath = std::filesystem::relative(filesystemPath, currentPath, error);
		if (!error && !relativePath.empty()) {
			std::string relative = relativePath.generic_string();
			if (!relative.starts_with(".."))
				return relative;
		}
	}
	return filesystemPath.generic_string();
}

} // namespace pathutil
