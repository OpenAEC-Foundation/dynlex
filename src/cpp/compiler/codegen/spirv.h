#pragma once
#include "parseContext.h"

// Emit SPIR-V binary from the LLVM module using LLVM's native SPIR-V backend
// Returns true on success, false on error (errors added to context.diagnostics)
#include <memory>
#include <string>
#include <string_view>

namespace llvm {
class TargetMachine;
}

struct ParseContext;

inline constexpr char shaderOutputMetadataName[] = "dynlex.shader.output";

std::string shaderInterpolantGlobalName(std::string_view interpolantName);
std::unique_ptr<llvm::TargetMachine> createSPIRVTargetMachine(ParseContext &context, std::string &errorMessage);
bool emitSPIRVModule(ParseContext &context);
