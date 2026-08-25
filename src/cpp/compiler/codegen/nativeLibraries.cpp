#include "nativeLibraries.h"
#include <array>

namespace {

struct LibraryNameMapping {
	llvm::StringLiteral portableName;
	llvm::StringLiteral linkerName;
};

} // namespace

std::vector<std::string>
nativeLibraryArguments(const llvm::Triple &targetTriple, llvm::StringRef library, llvm::StringRef runtimeLibraryPath) {
	if (library == "dynlex_runtime") {
		std::vector<std::string> arguments = {runtimeLibraryPath.str()};
		if (targetTriple.isOSWindows()) {
			arguments.push_back("-lshell32");
			arguments.push_back("-lole32");
			arguments.push_back("-luuid");
		} else {
			arguments.push_back("-pthread");
		}
		return arguments;
	}
	if (targetTriple.isOSDarwin() && library == "GL")
		return {"-framework", "OpenGL"};

	if (targetTriple.isOSWindows()) {
		static constexpr std::array windowsLibraryNames = {
			LibraryNameMapping{"GL", "opengl32"},
			LibraryNameMapping{"glfw", "glfw3dll"},
		};
		for (const LibraryNameMapping &mapping : windowsLibraryNames) {
			if (library == mapping.portableName) {
				library = mapping.linkerName;
				break;
			}
		}
	}

	return {"-l" + library.str()};
}
