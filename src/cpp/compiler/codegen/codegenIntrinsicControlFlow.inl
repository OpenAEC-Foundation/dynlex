[[gnu::noinline]] static CodegenResult generateExecuteBodyIntrinsic(ParseContext &context, Expression *callExpr) {
	Section *callSection = callExpr && callExpr->range.line ? callExpr->range.line->section : nullptr;
	if (!callSection)
		crashCompilerBug("execute body call is missing source section context");
	if (context.sectionFlexBodyFrames.empty())
		crashCompilerBug("execute body used outside of section flex expansion");

	Section *executionSection = nullptr;
	ParseContext::SectionFlexBodyFrame *targetFrame = resolveSectionFlexBodyFrame(
		context.sectionFlexBodyFrames, callSection, context.flexCallSiteSectionStack, context.activeFlexDefinitionStack,
		&executionSection
	);

	if (!targetFrame || !targetFrame->bodySection) {
		context.addDiagnostic(
			Diagnostic(context, Diagnostic::Level::Error, "execute body has no matching section flex body", callExpr->range)
		);
		return CodegenResult::failure();
	}

	const bool specializedTransfer = static_cast<bool>(callExpr->inferredCallerBody);
	InstantiatedSectionBody *originalBody = targetFrame->instantiatedBody;
	if (specializedTransfer) {
		targetFrame->instantiatedBody = callExpr->inferredCallerBody.get();
		targetFrame->bodyEmitted = false;
	}
	if (targetFrame->bodyEmitted) {
		context.addDiagnostic(Diagnostic(
			context, Diagnostic::Level::Error, "execute body can only run once per section flex call", callExpr->range
		));
		return CodegenResult::failure();
	}
	const size_t frameIndex = static_cast<size_t>(targetFrame - context.sectionFlexBodyFrames.data());
	if (!emitSectionFlexCallerBody(context, *targetFrame, executionSection, false))
		return CodegenResult::failure();
	if (specializedTransfer)
		context.sectionFlexBodyFrames[frameIndex].instantiatedBody = originalBody;
	return nullptr;
}

[[gnu::noinline]] static CodegenResult
generateLoopWhileIntrinsic(ParseContext &context, Expression *callExpr, const std::vector<Expression *> &args) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	Section *bodySection = context.currentBodySection;
	if (!bodySection) {
		context.addDiagnostic(Diagnostic(
			context, Diagnostic::Level::Error, "loop while requires body section", intrinsicDiagnosticRange(context, callExpr)
		));
		return CodegenResult::failure();
	}
	if (activeSectionFlexBodyFrame(context).openingExpression->staticLoopBodies)
		return nullptr;

	llvm::Function *func = builder.GetInsertBlock()->getParent();

	llvm::BasicBlock *condBlock = llvm::BasicBlock::Create(*context.llvmContext, "while_cond", func);
	llvm::BasicBlock *bodyBlock = llvm::BasicBlock::Create(*context.llvmContext, "while_body", func);
	llvm::BasicBlock *exitBlock = llvm::BasicBlock::Create(*context.llvmContext, "while_exit", func);

	builder.CreateBr(condBlock);
	builder.SetInsertPoint(condBlock);

	llvm::Value *condValue = nullptr;
	if (!generateIntrinsicRuntimeValue(context, args[1], condValue))
		return CodegenResult::failure();
	// Generating a condition can grow the frame stack through lazy function codegen.
	ParseContext::SectionFlexBodyFrame &bodyFrame = activeSectionFlexBodyFrame(context);
	requireCompilerInvariant(bodyFrame.openingExpression, "loop codegen frame has no opening expression");
	DataType condType = finalizedExpressionType(context, args[1]);
	if (condType.kind != DataType::Kind::Bool)
		crashCompilerBug("loop while condition must be boolean after type inference");
	emitExecutionTrace(context, bodyFrame.openingExpression->range, condValue);
	const bool *knownCondition =
		bodyFrame.openingExpression->sectionOutcome.read().kind == Expression::SectionOutcome::Kind::Loop
			? std::get_if<bool>(&bodyFrame.openingExpression->sectionOutcome.read().conditionValue)
			: nullptr;
	if (knownCondition) {
		builder.CreateBr(*knownCondition ? bodyBlock : exitBlock);
	} else {
		builder.CreateCondBr(condValue, bodyBlock, exitBlock);
	}

	builder.SetInsertPoint(bodyBlock);
	bodyFrame.exitBlock = exitBlock;
	bodyFrame.branchBackBlock = condBlock;

	return nullptr;
}

[[gnu::noinline]] static CodegenResult
generateIfIntrinsic(ParseContext &context, Expression *callExpr, const std::vector<Expression *> &args) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	Section *bodySection = context.currentBodySection;
	if (!bodySection) {
		context.addDiagnostic(Diagnostic(
			context, Diagnostic::Level::Error, "if requires body section", intrinsicDiagnosticRange(context, callExpr)
		));
		return CodegenResult::failure();
	}

	llvm::Function *func = builder.GetInsertBlock()->getParent();

	llvm::BasicBlock *thenBlock = llvm::BasicBlock::Create(*context.llvmContext, "if_then", func);
	llvm::BasicBlock *exitBlock = llvm::BasicBlock::Create(*context.llvmContext, "if_exit", func);

	llvm::Value *condValue = nullptr;
	if (!generateIntrinsicRuntimeValue(context, args[1], condValue))
		return CodegenResult::failure();
	DataType condType = finalizedExpressionType(context, args[1]);
	if (condType.kind != DataType::Kind::Bool)
		crashCompilerBug("if condition must be boolean after type inference");
	emitExecutionTrace(context, activeSectionFlexBodyFrame(context).openingExpression->range, condValue);
	builder.CreateCondBr(condValue, thenBlock, exitBlock);

	builder.SetInsertPoint(thenBlock);
	ParseContext::SectionFlexBodyFrame &bodyFrame = activeSectionFlexBodyFrame(context);
	bodyFrame.exitBlock = exitBlock;
	bodyFrame.branchBackBlock = nullptr;

	return nullptr;
}

[[gnu::noinline]] static CodegenResult generateAlternativeIntrinsic(
	ParseContext &context, Expression *callExpr, const std::vector<Expression *> &args, IntrinsicKind kind
) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	Section *bodySection = context.currentBodySection;
	if (!bodySection) {
		context.addDiagnostic(Diagnostic(
			context, Diagnostic::Level::Error, "else requires body section", intrinsicDiagnosticRange(context, callExpr)
		));
		return CodegenResult::failure();
	}

	llvm::Function *func = builder.GetInsertBlock()->getParent();
	llvm::BasicBlock *currentBlock = builder.GetInsertBlock();

	// Create new exit block — if/elif bodies will jump here (skipping the else)
	llvm::BasicBlock *newExitBlock = llvm::BasicBlock::Create(*context.llvmContext, "else_exit", func);

	// Redirect all unconditional branch predecessors to the new exit block.
	// Unconditional branches come from if/elif bodies (they should skip the else).
	// Conditional false-path branches come from if/elif conditions (they should fall through here).
	llvm::SmallVector<llvm::BasicBlock *, 4> uncondPreds;
	for (llvm::BasicBlock *pred : llvm::predecessors(currentBlock)) {
		if (llvm::isa<llvm::UncondBrInst>(pred->getTerminator())) {
			uncondPreds.push_back(pred);
		}
	}
	for (llvm::BasicBlock *pred : uncondPreds) {
		pred->getTerminator()->replaceUsesOfWith(currentBlock, newExitBlock);
	}

	if (kind == IntrinsicKind::ElseIf) {
		llvm::BasicBlock *elifThenBlock = llvm::BasicBlock::Create(*context.llvmContext, "elif_then", func);

		llvm::Value *condValue = nullptr;
		if (!generateIntrinsicRuntimeValue(context, args[1], condValue))
			return CodegenResult::failure();
		DataType condType = finalizedExpressionType(context, args[1]);
		if (condType.kind != DataType::Kind::Bool)
			crashCompilerBug("else if condition must be boolean after type inference");
		emitExecutionTrace(context, activeSectionFlexBodyFrame(context).openingExpression->range, condValue);
		builder.CreateCondBr(condValue, elifThenBlock, newExitBlock);

		builder.SetInsertPoint(elifThenBlock);
	}

	ParseContext::SectionFlexBodyFrame &bodyFrame = activeSectionFlexBodyFrame(context);
	bodyFrame.exitBlock = newExitBlock;
	bodyFrame.branchBackBlock = nullptr;

	return nullptr;
}

[[gnu::noinline]] static CodegenResult
generateSwitchIntrinsic(ParseContext &context, Expression *callExpr, const std::vector<Expression *> &args) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	Section *bodySection = context.currentBodySection;
	if (!bodySection) {
		context.addDiagnostic(Diagnostic(
			context, Diagnostic::Level::Error, "switch requires body section", intrinsicDiagnosticRange(context, callExpr)
		));
		return CodegenResult::failure();
	}
	llvm::Function *func = builder.GetInsertBlock()->getParent();

	llvm::Value *switchValue = nullptr;
	if (!generateIntrinsicRuntimeValue(context, args[1], switchValue))
		return CodegenResult::failure();

	// Ensure the value is an integer (LLVM switch requires integer operand)
	DataType switchType = finalizedExpressionType(context, args[1]);
	if (switchType.kind != DataType::Kind::Int && switchType.kind != DataType::Kind::Bool) {
		context.addDiagnostic(
			Diagnostic(context, Diagnostic::Level::Error, "switch requires integer", args[1] ? args[1]->range : Range())
		);
		return CodegenResult::failure();
	}

	llvm::BasicBlock *defaultBlock = llvm::BasicBlock::Create(*context.llvmContext, "switch_default", func);
	llvm::BasicBlock *exitBlock = llvm::BasicBlock::Create(*context.llvmContext, "switch_exit", func);

	llvm::SwitchInst *switchInst = builder.CreateSwitch(switchValue, defaultBlock);

	// Default case: just branch to exit
	builder.SetInsertPoint(defaultBlock);
	builder.CreateBr(exitBlock);

	// Store switch state for "case" intrinsics to use
	context.currentSwitchInst = switchInst;
	context.currentSwitchExitBlock = exitBlock;
	ParseContext::SectionFlexBodyFrame &bodyFrame = activeSectionFlexBodyFrame(context);
	bodyFrame.exitBlock = exitBlock;
	bodyFrame.branchBackBlock = nullptr;
	bodyFrame.continuationBlock = nullptr;
	builder.SetInsertPoint(exitBlock);

	return nullptr;
}

[[gnu::noinline]] static CodegenResult
generateCaseIntrinsic(ParseContext &context, Expression *callExpr, const std::vector<Expression *> &args) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	Section *bodySection = context.currentBodySection;
	if (!bodySection) {
		context.addDiagnostic(Diagnostic(
			context, Diagnostic::Level::Error, "case requires body section", intrinsicDiagnosticRange(context, callExpr)
		));
		return CodegenResult::failure();
	}

	if (!context.currentSwitchInst) {
		context.addDiagnostic(
			Diagnostic(context, Diagnostic::Level::Error, "case outside switch", intrinsicDiagnosticRange(context, callExpr))
		);
		return CodegenResult::failure();
	}

	llvm::Function *func = builder.GetInsertBlock()->getParent();

	std::optional<std::int64_t> caseValue =
		getCompileTimeIntegerValue(resolveStoredCompileTimeValue(args[1], context.flexBindingFrames));
	requireCompilerInvariant(caseValue.has_value(), "case reached codegen without an inferred constant integer value");
	llvm::Type *switchType = context.currentSwitchInst->getCondition()->getType();
	llvm::ConstantInt *caseConst = llvm::ConstantInt::get(llvm::cast<llvm::IntegerType>(switchType), *caseValue, true);

	llvm::BasicBlock *caseBlock = llvm::BasicBlock::Create(*context.llvmContext, "case", func);
	context.currentSwitchInst->addCase(caseConst, caseBlock);

	builder.SetInsertPoint(caseBlock);
	activeSectionFlexBodyFrame(context).continuationBlock = context.currentSwitchExitBlock;

	return nullptr;
}

[[gnu::noinline]] static CodegenResult generateDefaultCaseIntrinsic(ParseContext &context, Expression *callExpr) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	Section *bodySection = context.currentBodySection;
	if (!bodySection) {
		context.addDiagnostic(Diagnostic(
			context, Diagnostic::Level::Error, "default case requires body section", intrinsicDiagnosticRange(context, callExpr)
		));
		return CodegenResult::failure();
	}

	if (!context.currentSwitchInst) {
		context.addDiagnostic(Diagnostic(
			context, Diagnostic::Level::Error, "default case outside switch", intrinsicDiagnosticRange(context, callExpr)
		));
		return CodegenResult::failure();
	}

	if (!context.switchesWithDefaultCase.insert(context.currentSwitchInst).second) {
		context.addDiagnostic(
			Diagnostic(context, Diagnostic::Level::Error, "duplicate default case", intrinsicDiagnosticRange(context, callExpr))
		);
		return CodegenResult::failure();
	}

	// Reuse the switch's default block: drop its placeholder branch to the
	// exit and let the body fill it instead.
	llvm::BasicBlock *defaultBlock = context.currentSwitchInst->getDefaultDest();
	defaultBlock->getTerminator()->eraseFromParent();
	builder.SetInsertPoint(defaultBlock);
	activeSectionFlexBodyFrame(context).continuationBlock = context.currentSwitchExitBlock;

	return nullptr;
}

[[gnu::noinline]] static CodegenResult generateReturnIntrinsic(ParseContext &context, const std::vector<Expression *> &args) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	if (args.size() <= 1) {
		llvm::Function *currentFunction = builder.GetInsertBlock()->getParent();
		if (!releaseManagedStorageForReturn(context))
			return CodegenResult::failure();
		if (currentFunction == context.mainLLVMFunction) {
			requireCompilerInvariant(context.mainCleanupBlock, "main return requires an initialized cleanup block");
			if (!context.options.emitSPIRV) {
				requireCompilerInvariant(context.mainReturnStorage, "native main return requires initialized return storage");
				builder.CreateStore(builder.getInt32(0), context.mainReturnStorage);
			}
			builder.CreateBr(context.mainCleanupBlock);
		} else {
			builder.CreateRetVoid();
		}
		return nullptr;
	}
	llvm::Value *returnValue = nullptr;
	if (!generateIntrinsicRuntimeValue(context, args[1], returnValue))
		return CodegenResult::failure();
	DataType returnType = finalizedExpressionType(context, args[1]);
	bool managedReturn = typeHasManagedLifecycle(returnType);
	bool ownedReturn = managedReturn && managedExpressionResultIsOwned(context, args[1]);
	llvm::Function *currentFunction = builder.GetInsertBlock()->getParent();
	if (currentFunction == context.mainLLVMFunction) {
		requireCompilerInvariant(context.mainCleanupBlock, "main return requires an initialized cleanup block");
		if (context.options.emitSPIRV) {
			if (ownedReturn && !releaseManagedValue(context, returnType, returnValue))
				return CodegenResult::failure();
			if (!releaseManagedStorageForReturn(context))
				return CodegenResult::failure();
			builder.CreateBr(context.mainCleanupBlock);
			return nullptr;
		}
		requireCompilerInvariant(context.mainReturnStorage, "native main return requires initialized return storage");
		if (managedReturn && !ownedReturn && !retainManagedValue(context, returnType, returnValue))
			return CodegenResult::failure();
		DataType mainReturnType{DataType::Kind::Int, 4};
		returnValue = ensureType(context, returnValue, returnType, mainReturnType);
		if (!releaseManagedStorageForReturn(context))
			return CodegenResult::failure();
		builder.CreateStore(returnValue, context.mainReturnStorage);
		builder.CreateBr(context.mainCleanupBlock);
		return nullptr;
	}
	requireCompilerInvariant(
		context.currentCodegenInstantiation != nullptr, "function return reached codegen without an active instantiation"
	);
	DataType functionReturnType = context.currentCodegenInstantiation->returnType;
	returnValue = ensureType(context, returnValue, returnType, functionReturnType);
	returnType = functionReturnType;
	managedReturn = typeHasManagedLifecycle(returnType);
	ownedReturn = managedReturn && managedExpressionResultIsOwned(context, args[1]);
	if (managedReturn && !ownedReturn && !retainManagedValue(context, returnType, returnValue))
		return CodegenResult::failure();
	if (!releaseManagedStorageForReturn(context))
		return CodegenResult::failure();
	builder.CreateRet(returnValue);
	return nullptr;
}
