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
std::string shaderResourceGlobalName(std::string_view name);
std::string shaderSampleFunctionName(const ShaderResourceValue &image, const ShaderResourceValue &sampler);
std::string shaderVertexInputGlobalName(std::string_view name, std::uint32_t location, const DataType &type);
std::unique_ptr<llvm::TargetMachine> createSPIRVTargetMachine(ParseContext &context, std::string &errorMessage);
bool emitSPIRVModule(ParseContext &context);
