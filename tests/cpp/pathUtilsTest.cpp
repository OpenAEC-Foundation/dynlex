#include "pathUtils.h"
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace {

void expect(bool condition, std::string_view message) {
	if (condition)
		return;
	std::cerr << message << '\n';
	std::exit(1);
}

} // namespace

int main() {
	const std::filesystem::path path =
		(std::filesystem::current_path() / "path utils URI fixture.txt").lexically_normal();
	const std::string uri = pathutil::toAbsoluteUri(path.string());
	expect(uri.starts_with("file:///"), "absolute paths did not produce local file URIs");
	expect(uri.find('\\') == std::string::npos, "file URI contains a platform path separator");
	expect(uri.find("%20") != std::string::npos, "file URI did not encode spaces");
	expect(std::filesystem::path(pathutil::toFilesystemPath(uri)) == path, "file URI did not round-trip to its path");
	expect(pathutil::toAbsoluteUri(uri) == uri, "canonical file URI changed during normalization");
	return 0;
}
