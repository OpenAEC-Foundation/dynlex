#pragma once

#include "llvm/ADT/StringRef.h"
#include "llvm/TargetParser/Triple.h"
#include <string>
#include <unordered_set>
#include <vector>

std::vector<std::string> orderedNativeLibraries(const std::unordered_set<std::string> &libraries);

std::vector<std::string>
nativeLibraryArguments(const llvm::Triple &targetTriple, llvm::StringRef library, llvm::StringRef runtimeLibraryPath);
