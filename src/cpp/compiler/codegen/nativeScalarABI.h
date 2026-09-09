#pragma once

#include "type.h"
#include "llvm/IR/Attributes.h"
#include "llvm/TargetParser/Triple.h"
#include <span>

// AAPCS64 leaves scalar bool values unextended. The other native ABIs used
// here (including Darwin AArch64 and WebAssembly) require canonical bools.
// Apply the same contract to definitions, declarations, and call sites.
template <typename Callable>
void applyNativeScalarABI(
	Callable &callable, const llvm::Triple &target, const DataType &result, std::span<const DataType> arguments
) {
	if (target.isAArch64() && !target.isOSDarwin())
		return;
	auto isBoolean = [](const DataType &type) {
		return !type.isPointer() && type.kind == DataType::Kind::Bool;
	};
	if (isBoolean(result))
		callable.addRetAttr(llvm::Attribute::ZExt);
	for (unsigned index = 0; index < arguments.size(); index++)
		if (isBoolean(arguments[index]))
			callable.addParamAttr(index, llvm::Attribute::ZExt);
}
