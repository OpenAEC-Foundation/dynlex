#pragma once
#include "parseContext.h"
#include "llvm/IR/IRBuilder.h"

// Function-local addresses, ownership and bindings cannot escape to a caller or
// another generated ABI for the same inferred body.
struct FunctionCodegenScope {
	ParseContext &context;
	llvm::IRBuilder<>::InsertPoint insertionPoint;
	llvm::DebugLoc debugLocation;
	llvm::DIScope *debugScope;
	decltype(ParseContext::codegenParameterBindings) parameterBindings;
	decltype(ParseContext::codegenParameterDefinitions) parameterDefinitions;
	decltype(ParseContext::variableStorage) variableStorage;
	decltype(ParseContext::subjectStorage) subjectStorage;
	decltype(ParseContext::localDataLLVMValues) localDataValues;
	llvm::Value *lifecycleValue;
	const Instantiation *instantiation;
	unsigned staticReferenceDepth;
	InstantiatedSectionBody *instantiatedBody;
	BindingFrameStack flexBindings;
	decltype(ParseContext::flexCallSiteRangeStack) callSiteRanges;
	decltype(ParseContext::managedLocalStorage) localStorage;
	decltype(ParseContext::managedFunctionStorage) functionStorage;
	llvm::BasicBlock *returnBlock;
	llvm::Value *returnStorage;

	FunctionCodegenScope(ParseContext &ctx, llvm::DIScope *savedDebugScope)
		: context(ctx), insertionPoint(static_cast<llvm::IRBuilder<> &>(*ctx.llvmBuilder).saveIP()),
		  debugLocation(static_cast<llvm::IRBuilder<> &>(*ctx.llvmBuilder).getCurrentDebugLocation()),
		  debugScope(savedDebugScope), parameterBindings(std::move(ctx.codegenParameterBindings)),
		  parameterDefinitions(std::move(ctx.codegenParameterDefinitions)), variableStorage(ctx.variableStorage),
		  subjectStorage(std::move(ctx.subjectStorage)), localDataValues(std::move(ctx.localDataLLVMValues)),
		  lifecycleValue(ctx.managedLifecycleValueBinding), instantiation(ctx.currentCodegenInstantiation),
		  staticReferenceDepth(ctx.staticReferenceDepth), instantiatedBody(ctx.currentInstantiatedSectionBody),
		  flexBindings(ctx.flexBindingFrames), callSiteRanges(std::move(ctx.flexCallSiteRangeStack)),
		  localStorage(std::move(ctx.managedLocalStorage)), functionStorage(std::move(ctx.managedFunctionStorage)),
		  returnBlock(ctx.managedReturnBlock), returnStorage(ctx.managedReturnStorage) {
		ctx.codegenParameterBindings.clear();
		ctx.codegenParameterDefinitions.clear();
		ctx.subjectStorage.clear();
		ctx.localDataLLVMValues.clear();
		ctx.managedLifecycleValueBinding = nullptr;
		ctx.currentCodegenInstantiation = nullptr;
		ctx.staticReferenceDepth = 0;
		ctx.currentInstantiatedSectionBody = nullptr;
		ctx.flexBindingFrames = makeBindingFrameStack(BindingFrame{});
		ctx.flexCallSiteRangeStack.clear();
		ctx.managedLocalStorage.clear();
		ctx.managedFunctionStorage.clear();
		ctx.managedReturnBlock = nullptr;
		ctx.managedReturnStorage = nullptr;
	}
	~FunctionCodegenScope() {
		context.codegenParameterBindings = std::move(parameterBindings);
		context.codegenParameterDefinitions = std::move(parameterDefinitions);
		context.variableStorage = std::move(variableStorage);
		context.subjectStorage = std::move(subjectStorage);
		context.localDataLLVMValues = std::move(localDataValues);
		context.managedLifecycleValueBinding = lifecycleValue;
		context.currentCodegenInstantiation = instantiation;
		context.staticReferenceDepth = staticReferenceDepth;
		context.currentInstantiatedSectionBody = instantiatedBody;
		context.currentDebugScope = debugScope;
		context.flexBindingFrames = std::move(flexBindings);
		context.flexCallSiteRangeStack = std::move(callSiteRanges);
		context.managedLocalStorage = std::move(localStorage);
		context.managedFunctionStorage = std::move(functionStorage);
		context.managedReturnBlock = returnBlock;
		context.managedReturnStorage = returnStorage;
		auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
		builder.restoreIP(insertionPoint);
		builder.SetCurrentDebugLocation(debugLocation);
	}
};
