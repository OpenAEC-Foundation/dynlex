#pragma once
#include <string>
#include <vector>
struct SourceFile {
	SourceFile(std::string uri, std::string content) : uri(uri), content(content) {}
	std::string uri;
	std::string content;
};