#include "nativeLibraries.h"

std::vector<std::string>
nativeLibraryArguments(const llvm::Triple &targetTriple, llvm::StringRef library, llvm::StringRef runtimeLibraryPath) {
	if (targetTriple.isOSWindows() && library == "glfw")
		return {"-lglfw3dll"};
	if (targetTriple.isOSWindows() && library == "vulkan")
		return {"-lvulkan-1"};
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

	return {"-l" + library.str()};
}
