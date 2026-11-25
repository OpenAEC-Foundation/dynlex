#pragma once

#include "parseContext.h"
#include <optional>
#include <string>

struct PackageImport {
	std::string path;
	std::string root;
};

// A handled import must not continue through the filesystem search paths.
bool resolvePackageImport(
	const std::string &requested, const std::string &importer, ParseContext &context, std::optional<PackageImport> &resolved,
	bool &handled, Range site
);
