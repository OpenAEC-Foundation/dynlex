#include "targetOptions.h"

llvm::CodeGenOptLevel codeGenerationOptimizationLevel(const ParseContext::Options &options) {
	switch (options.optimizationLevel) {
	case 0:
		return llvm::CodeGenOptLevel::None;
	case 1:
		return llvm::CodeGenOptLevel::Less;
	case 2:
		return llvm::CodeGenOptLevel::Default;
	case 3:
		return llvm::CodeGenOptLevel::Aggressive;
	}
	crashCompilerBug("invalid code generation optimization level");
}

llvm::TargetOptions llvmTargetOptions(const ParseContext::Options &options) {
	llvm::TargetOptions targetOptions;
	targetOptions.AllowFPOpFusion = options.floatingPointContract == ParseContext::FloatingPointContract::Fast
										? llvm::FPOpFusion::Fast
										: llvm::FPOpFusion::Strict;
	return targetOptions;
}
