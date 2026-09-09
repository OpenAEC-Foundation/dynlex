#pragma once

#include "type.h"
#include "llvm/IR/Attributes.h"
#include "llvm/TargetParser/Triple.h"
#include <span>

// AAPCS64 leaves narrow scalar values unextended. Win64 extends bools only;
// the other native ABIs here also extend 8/16-bit integers by signedness.
// Apply the same contract to definitions, declarations, and call sites.
template <typename Callable>
void applyNativeScalarABI(
	Callable &callable, const llvm::Triple &target, const DataType &result, std::span<const DataType> arguments
) {
	if (target.isAArch64() && !target.isOSDarwin())
		return;
	auto extension = [&](const DataType &type) {
		if (!type.isPointer() && type.kind == DataType::Kind::Bool)
			return llvm::Attribute::ZExt;
		if (type.isInteger() && type.numericSize < 4 && !(target.getArch() == llvm::Triple::x86_64 && target.isOSWindows()))
			return type.kind == DataType::Kind::Int ? llvm::Attribute::SExt : llvm::Attribute::ZExt;
		return llvm::Attribute::None;
	};
	if (auto attribute = extension(result); attribute != llvm::Attribute::None)
		callable.addRetAttr(attribute);
	for (unsigned index = 0; index < arguments.size(); index++)
		if (auto attribute = extension(arguments[index]); attribute != llvm::Attribute::None)
			callable.addParamAttr(index, attribute);
}
