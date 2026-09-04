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

	return {"-l" + library.str()};
}
