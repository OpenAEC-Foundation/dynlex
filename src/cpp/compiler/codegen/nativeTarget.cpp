#include "nativeTarget.h"

llvm::Triple normalizedNativeTargetTriple(llvm::StringRef triple) { return llvm::Triple(llvm::Triple::normalize(triple)); }
