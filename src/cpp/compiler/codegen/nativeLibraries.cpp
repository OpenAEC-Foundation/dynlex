#include "nativeLibraries.h"

std::vector<std::string> nativeLibraryArguments(
	const llvm::Triple &targetTriple, llvm::StringRef library, llvm::StringRef runtimeLibraryPath,
	llvm::StringRef graphicsLibraryPath
) {
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
	if (library == "dynlex_graphics") {
		std::vector<std::string> arguments = {graphicsLibraryPath.str(), runtimeLibraryPath.str()};
		if (targetTriple.isOSWindows()) {
			arguments.insert(arguments.end(), {"-lglfw3dll", "-lvulkan-1", "-lshell32", "-lole32", "-luuid"});
		} else {
			arguments.insert(arguments.end(), {"-lglfw", "-lvulkan"});
			if (!targetTriple.isOSDarwin())
				arguments.push_back("-lm");
			arguments.push_back("-pthread");
		}
		return arguments;
	}

	// Programs that call GLFW and the Vulkan loader directly (`"glfw"`,
	// `"vulkan"`) link the names the Windows packages install them under.
	// zlib is named by its Linux runtime file (`":libz.so.1"`); the Windows
	// package's import library is `z`.
	if (targetTriple.isOSWindows()) {
		if (library == "glfw")
			return {"-lglfw3dll"};
		if (library == "vulkan")
			return {"-lvulkan-1"};
		if (library == ":libz.so.1")
			return {"-lz"};
		if (library == ":libpng16.so.16")
			return {"-llibpng16"};
	}
	return {"-l" + library.str()};
}
