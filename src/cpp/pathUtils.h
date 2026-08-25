#pragma once
#include <string>
#include <string_view>

namespace pathutil {

inline constexpr std::string_view fileUriScheme = "file://";

inline bool isFileUri(std::string_view uriOrPath) { return uriOrPath.starts_with(fileUriScheme); }

std::string toFilesystemPath(std::string_view uriOrPath);
std::string toAbsoluteUri(std::string_view uriOrPath);
std::string toDisplayPath(std::string_view uriOrPath);

} // namespace pathutil
