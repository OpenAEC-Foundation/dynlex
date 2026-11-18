#pragma once
#include "classDefinition.h"
#include "codeLine.h"
#include "parseContext.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/Module.h"
#include <map>

inline constexpr const char *debugTypeProductsName = "dynlex.debug.types";

inline void recordDebugTypeProduct(ParseContext &context, const DataType &type, llvm::DICompositeType *metadata) {
	if (!context.recordGeneratedFunction)
		return;
	const auto *opening = type.classDefinition->range.line->sectionOpening;
	std::string identity = context.functionSymbolIdentities.at(opening) + "/" + std::to_string(type.classInstIndex);
	llvm::Metadata *operands[] = {llvm::MDString::get(*context.llvmContext, identity), metadata};
	context.llvmModule->getOrInsertNamedMetadata(debugTypeProductsName)
		->addOperand(llvm::MDNode::get(*context.llvmContext, operands));
}

inline void sortDebugTypeProducts(llvm::Module &module) {
	auto *products = module.getNamedMetadata(debugTypeProductsName);
	if (!products)
		return;
	std::map<std::string, llvm::MDNode *> sorted;
	for (llvm::MDNode *record : products->operands()) {
		std::string identity = llvm::cast<llvm::MDString>(record->getOperand(0))->getString().str();
		requireCompilerInvariant(sorted.emplace(identity, record).second, "duplicate concrete debug type product");
	}
	products->clearOperands();
	for (const auto &[identity, record] : sorted) {
		(void)identity;
		products->addOperand(record);
	}
}
