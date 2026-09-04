#pragma once

#include "llvm/ADT/StringRef.h"
#include "llvm/TargetParser/Triple.h"
#include <string>
#include <vector>

std::vector<std::string> nativeLibraryArguments(
	const llvm::Triple &targetTriple, llvm::StringRef library, llvm::StringRef runtimeLibraryPath,
	llvm::StringRef graphicsLibraryPath
);
