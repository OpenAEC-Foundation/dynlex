#pragma once

#include "parseContext.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Metadata.h"

inline constexpr llvm::StringLiteral requiredLibraryMetadataName = "dynlex.required_libraries";

inline void recordRequiredLibrary(ParseContext &context, llvm::StringRef library) {
	if (library.empty() || library == "libc")
		return;
	context.requiredLibraries.insert(library.str());
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	llvm::Function *function = builder.GetInsertBlock()->getParent();
	llvm::SmallVector<llvm::Metadata *, 4> libraries;
	if (const llvm::MDNode *node = function->getMetadata(requiredLibraryMetadataName)) {
		for (const llvm::MDOperand &operand : node->operands()) {
			auto *existing = llvm::dyn_cast<llvm::MDString>(operand.get());
			requireCompilerInvariant(existing, "invalid required-library metadata");
			if (existing->getString() == library)
				return;
			libraries.push_back(existing);
		}
	}
	libraries.push_back(llvm::MDString::get(*context.llvmContext, library));
	function->setMetadata(requiredLibraryMetadataName, llvm::MDNode::get(*context.llvmContext, libraries));
}

inline void restoreRequiredLibraries(ParseContext &context, const llvm::Function &function) {
	const llvm::MDNode *node = function.getMetadata(requiredLibraryMetadataName);
	if (!node)
		return;
	for (const llvm::MDOperand &operand : node->operands()) {
		const auto *library = llvm::dyn_cast<llvm::MDString>(operand.get());
		requireCompilerInvariant(library, "invalid required-library metadata");
		context.requiredLibraries.insert(library->getString().str());
	}
}
