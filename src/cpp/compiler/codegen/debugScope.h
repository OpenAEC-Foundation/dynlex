#pragma once
#include "parseContext.h"
#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/DebugInfoMetadata.h"

inline llvm::DIScope *debugScopeForFile(ParseContext &context, llvm::DIFile *file) {
	llvm::DIScope *scope = context.currentDebugScope;
	if (file != scope->getFile())
		scope = context.diBuilder->createLexicalBlockFile(scope, file);
	return scope;
}
