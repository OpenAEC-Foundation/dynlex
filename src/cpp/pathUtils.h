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

inline std::string toDisplayPath(std::string_view uriOrPath) {
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
