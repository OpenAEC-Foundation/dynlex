#pragma once

#include "llvm/ADT/StringRef.h"
#include "llvm/TargetParser/Triple.h"

llvm::Triple normalizedNativeTargetTriple(llvm::StringRef triple);
