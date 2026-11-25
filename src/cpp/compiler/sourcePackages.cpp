#include "sourcePackages.h"
#include "lsp/fileSystem.h"
#include "pathUtils.h"
#include <filesystem>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace {
using Json = nlohmann::json;
namespace fs = std::filesystem;

bool inside(const fs::path &path, const fs::path &root) {
	auto relative = path.lexically_relative(root);
	return !relative.empty() && *relative.begin() != "..";
}

std::string packagePath(const Json &value) {
	const auto result = value.get<std::string>();
	fs::path path(result);
	if (result.empty() || path.is_absolute() || path.generic_string() != result ||
		path.lexically_normal().generic_string() != result || result.find('\\') != std::string::npos ||
		result.find(':') != std::string::npos)
		throw std::runtime_error("A library path is not a normalized relative path");
	for (const auto &part : path)
		if (part == ".." || part == ".")
			throw std::runtime_error("A library path escapes its package");
	return result;
}
} // namespace

bool resolvePackageImport(
	const std::string &requested, const std::string &importer, ParseContext &context, std::optional<PackageImport> &resolved,
	bool &handled, Range site
) {
	handled = false;
	if (requested.starts_with("./") || requested.starts_with("../") || fs::path(requested).is_absolute())
		return true;
	fs::path source = fs::absolute(pathutil::toFilesystemPath(importer)).lexically_normal();
	fs::path directory = source.parent_path();
	// Installed sources belong to the consuming project's lock. A package may
	// distribute its own application manifest without changing that ownership.
	for (auto ancestor = directory; !ancestor.empty(); ancestor = ancestor.parent_path()) {
		if (ancestor.filename() == "packages" && ancestor.parent_path().filename() == ".dynlex") {
			directory = ancestor.parent_path().parent_path();
			break;
		}
		if (ancestor == ancestor.parent_path())
			break;
	}
	lsp::SourceFile *manifestFile = nullptr;
	while (!directory.empty()) {
		manifestFile = context.fileSystem->getFile((directory / "dynlex.json").string());
		if (manifestFile)
			break;
		auto parent = directory.parent_path();
		if (parent == directory)
			break;
		directory = parent;
	}
	if (!manifestFile)
		return true;
	try {
		auto manifest = Json::parse(manifestFile->content);
		if (manifest.at("schema") != 1 || !manifest.at("dependencies").is_object())
			throw std::runtime_error("Unsupported DynLex project manifest");
		auto *lockFile = context.fileSystem->getFile((directory / "dynlex.lock").string());
		if (!lockFile && manifest.at("dependencies").empty())
			return true;
		if (!lockFile)
			throw std::runtime_error("Project dependencies have no lockfile; restore the project libraries");
		auto lock = Json::parse(lockFile->content);
		if (lock.at("schema") != 1 || !lock.at("packages").is_object() || !lock.at("dependencies").is_object())
			throw std::runtime_error("Unsupported DynLex dependency lock");
		if (lock.at("dependencies").size() != manifest.at("dependencies").size())
			throw std::runtime_error("The dependency lock does not match the project manifest");
		for (const auto &[alias, request] : manifest.at("dependencies").items()) {
			auto name = lock.at("dependencies").at(alias).get<std::string>();
			if (!lock.at("packages").contains(name) || lock.at("requests").at(alias) != request)
				throw std::runtime_error("The dependency lock does not match the project manifest");
		}
		Json dependencies = lock.at("dependencies");
		for (const auto &[name, entry] : lock.at("packages").items()) {
			auto root = (directory / packagePath(entry.at("root"))).lexically_normal();
			if (inside(source, root)) {
				dependencies = entry.at("dependencies");
				break;
			}
		}
		auto separator = requested.find('/');
		auto alias = requested.substr(0, separator);
		if (!dependencies.contains(alias))
			return true;
		handled = true;
		auto name = dependencies.at(alias).get<std::string>();
		auto &entry = lock.at("packages").at(name);
		auto root = (directory / packagePath(entry.at("root"))).lexically_normal();
		if (entry.at("root") != ".dynlex/packages/" + name)
			throw std::runtime_error("The library root does not match its package name");
		auto subpath = separator == std::string::npos ? std::string("index.dl") : requested.substr(separator + 1);
		packagePath(subpath);
		std::optional<std::string> target;
		size_t specificity = 0;
		for (const auto &[key, value] : entry.at("exports").items()) {
			auto destination = value.get<std::string>();
			if (key == subpath) {
				target = destination;
				break;
			}
			if (key.ends_with('*') && destination.ends_with('*')) {
				auto prefix = key.substr(0, key.size() - 1);
				if (subpath.starts_with(prefix) && (!target || prefix.size() > specificity)) {
					target = destination.substr(0, destination.size() - 1) + subpath.substr(prefix.size());
					specificity = prefix.size();
				}
			}
		}
		if (!target)
			throw std::runtime_error("The library does not export " + subpath);
		resolved = PackageImport{(root / packagePath(*target)).string(), root.string()};
		return true;
	} catch (const std::exception &error) {
		handled = true;
		context.addDiagnostic(
			Diagnostic(context, Diagnostic::Level::Error, "project dependency error", site, "detail", error.what())
		);
		return false;
	}
}
