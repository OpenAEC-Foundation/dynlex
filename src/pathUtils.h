#pragma once
#include <filesystem>
#include <string>
#include <string_view>

namespace pathutil {

inline constexpr std::string_view fileUriScheme = "file://";

inline bool isFileUri(std::string_view uriOrPath) { return uriOrPath.starts_with(fileUriScheme); }

inline std::string toFilesystemPath(std::string_view uriOrPath) {
	if (isFileUri(uriOrPath))
		return std::string(uriOrPath.substr(fileUriScheme.size()));
	return std::string(uriOrPath);
}

inline std::string toAbsoluteUri(std::string_view uriOrPath) {
	if (isFileUri(uriOrPath))
		return std::string(uriOrPath);
	return std::string(fileUriScheme) + std::filesystem::absolute(toFilesystemPath(uriOrPath)).string();
}

} // namespace pathutil
