#pragma once
#include "parseContext.h"

// Emit SPIR-V binary from the LLVM module using LLVM's native SPIR-V backend
// Returns true on success, false on error (errors added to context.diagnostics)
bool emitSPIRVModule(ParseContext &context);
