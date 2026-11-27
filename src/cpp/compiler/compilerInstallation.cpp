#include "compilerInstallation.h"
#ifndef DYNLEX_WEB
#include "llvm/Support/FileSystem.h"
#include <cstdint>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif
#endif

const CompilerInstallation &compilerInstallation() {
	static const CompilerInstallation installation = []() -> CompilerInstallation {
#ifdef DYNLEX_WEB
		return {"/", "/", false};
#else
		const auto executable = std::filesystem::canonical(
			llvm::sys::fs::getMainExecutable(
				nullptr, reinterpret_cast<void *>(reinterpret_cast<std::uintptr_t>(&compilerInstallation))
			)
		);
		const auto directory = executable.parent_path();
		const std::filesystem::path buildDirectory(DYNLEX_BUILD_BINARY_DIRECTORY);
#ifdef _WIN32
		const bool buildTree = CompareStringOrdinal(directory.c_str(), -1, buildDirectory.c_str(), -1, TRUE) == CSTR_EQUAL;
#else
		const bool buildTree = directory == buildDirectory;
#endif
		return {
			(directory / DYNLEX_PREFIX_FROM_BINARY_DIRECTORY).lexically_normal(),
			buildTree ? std::filesystem::path(PROJECT_SOURCE_DIR)
					  : (directory / DYNLEX_DATA_FROM_BINARY_DIRECTORY).lexically_normal(),
			!buildTree,
		};
#endif
	}();
	return installation;
}
