#pragma once
#include "parseContext.h"

// Emit native executable from the LLVM module
// Returns true on success, false on error (errors added to context.diagnostics)
bool emitNativeExecutable(ParseContext &context);
