#pragma once
#include <filesystem>

struct CompilerInstallation {
	std::filesystem::path prefix;
	std::filesystem::path sourceRoot;
	bool installed;
};

const CompilerInstallation &compilerInstallation();
