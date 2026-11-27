#include "compilerInstallation.h"
#ifndef DYNLEX_WEB
#include "llvm/Support/FileSystem.h"
#include <cstdint>
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
		// A packaged compiler's build directory may not exist or be accessible
		// on this machine. Only that directory's identity selects build resources.
		std::error_code buildDirectoryError;
		const bool buildTree = std::filesystem::equivalent(directory, DYNLEX_BUILD_BINARY_DIRECTORY, buildDirectoryError);
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
