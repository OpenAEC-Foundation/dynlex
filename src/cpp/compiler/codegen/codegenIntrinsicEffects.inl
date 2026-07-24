if (kind == IntrinsicKind::ExecuteBody) {
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

	if (targetFrame->bodyEmitted) {
		context.addDiagnostic(Diagnostic(
			context, Diagnostic::Level::Error, "execute body can only run once per section flex call", callExpr->range
		));
		return CodegenResult::failure();
	}
	if (!emitSectionFlexCallerBody(context, *targetFrame, executionSection, false))
		return CodegenResult::failure();
	return nullptr;
}

if (kind == IntrinsicKind::LoopWhile) {
	Section *bodySection = context.currentBodySection;
	if (!bodySection) {
		context.addDiagnostic(Diagnostic(
			context, Diagnostic::Level::Error, "loop while requires body section", intrinsicDiagnosticRange(context, callExpr)
		));
		return CodegenResult::failure();
	}

	llvm::Function *func = builder.GetInsertBlock()->getParent();

	llvm::BasicBlock *condBlock = llvm::BasicBlock::Create(*context.llvmContext, "while_cond", func);
	llvm::BasicBlock *bodyBlock = llvm::BasicBlock::Create(*context.llvmContext, "while_body", func);
	llvm::BasicBlock *exitBlock = llvm::BasicBlock::Create(*context.llvmContext, "while_exit", func);

	builder.CreateBr(condBlock);
	builder.SetInsertPoint(condBlock);

	ParseContext::SectionFlexBodyFrame &bodyFrame = activeSectionFlexBodyFrame(context);
	requireCompilerInvariant(bodyFrame.openingExpression, "loop codegen frame has no opening expression");
	llvm::Value *condValue = nullptr;
	if (!generateRuntimeValue(args[1], condValue))
		return CodegenResult::failure();
	DataType condType = finalizedExpressionType(context, args[1]);
	if (condType.kind != DataType::Kind::Bool)
		crashCompilerBug("loop while condition must be boolean after type inference");
	const bool *knownCondition = bodyFrame.openingExpression->sectionOutcome.kind == Expression::SectionOutcome::Kind::Loop
									 ? std::get_if<bool>(&bodyFrame.openingExpression->sectionOutcome.conditionValue)
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

if (kind == IntrinsicKind::If) {
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
	if (!generateRuntimeValue(args[1], condValue))
		return CodegenResult::failure();
	DataType condType = finalizedExpressionType(context, args[1]);
	if (condType.kind != DataType::Kind::Bool)
		crashCompilerBug("if condition must be boolean after type inference");
	builder.CreateCondBr(condValue, thenBlock, exitBlock);

	builder.SetInsertPoint(thenBlock);
	ParseContext::SectionFlexBodyFrame &bodyFrame = activeSectionFlexBodyFrame(context);
	bodyFrame.exitBlock = exitBlock;
	bodyFrame.branchBackBlock = nullptr;

	return nullptr;
}

if (kind == IntrinsicKind::Else || kind == IntrinsicKind::ElseIf) {
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
		llvm::BranchInst *br = llvm::dyn_cast<llvm::BranchInst>(pred->getTerminator());
		if (br && br->isUnconditional()) {
			uncondPreds.push_back(pred);
		}
	}
	for (llvm::BasicBlock *pred : uncondPreds) {
		pred->getTerminator()->replaceUsesOfWith(currentBlock, newExitBlock);
	}

	if (kind == IntrinsicKind::ElseIf) {
		llvm::BasicBlock *elifThenBlock = llvm::BasicBlock::Create(*context.llvmContext, "elif_then", func);

		llvm::Value *condValue = nullptr;
		if (!generateRuntimeValue(args[1], condValue))
			return CodegenResult::failure();
		DataType condType = finalizedExpressionType(context, args[1]);
		if (condType.kind != DataType::Kind::Bool)
			crashCompilerBug("else if condition must be boolean after type inference");
		builder.CreateCondBr(condValue, elifThenBlock, newExitBlock);

		builder.SetInsertPoint(elifThenBlock);
	}

	ParseContext::SectionFlexBodyFrame &bodyFrame = activeSectionFlexBodyFrame(context);
	bodyFrame.exitBlock = newExitBlock;
	bodyFrame.branchBackBlock = nullptr;

	return nullptr;
}

if (kind == IntrinsicKind::Switch) {
	Section *bodySection = context.currentBodySection;
	if (!bodySection) {
		context.addDiagnostic(Diagnostic(
			context, Diagnostic::Level::Error, "switch requires body section", intrinsicDiagnosticRange(context, callExpr)
		));
		return CodegenResult::failure();
	}
	llvm::Function *func = builder.GetInsertBlock()->getParent();

	llvm::Value *switchValue = nullptr;
	if (!generateRuntimeValue(args[1], switchValue))
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

if (kind == IntrinsicKind::Case) {
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

if (kind == IntrinsicKind::DefaultCase) {
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

if (kind == IntrinsicKind::Return) {
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
	if (!generateRuntimeValue(args[1], returnValue))
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
	if (managedReturn && !ownedReturn && !retainManagedValue(context, returnType, returnValue))
		return CodegenResult::failure();
	if (!releaseManagedStorageForReturn(context))
		return CodegenResult::failure();
	builder.CreateRet(returnValue);
	return nullptr;
}

if (isExternalCallIntrinsicKind(kind)) {
	// call: args[1]="library", args[2]="function", args[3]="return type", args[4+]=actual args
	// variadic call: the fixed argument count is args[4], and actual args begin at args[5].
	std::string library = getStringLiteral(args[1]);
	std::string funcName = getStringLiteral(args[2]);
	if (!library.empty() && library != "libc")
		context.requiredLibraries.insert(library);

	DataType retTypeRef = finalizedExpressionType(context, args[3]);
	DataType returnType = retTypeRef.toReferencedType();
	llvm::Type *returnLLVMType = getLLVMType(context, returnType);

	// Build call arguments — string literals become global constant pointers
	std::vector<llvm::Value *> callArgs;
	std::vector<DataType> callArgumentTypes;
	std::vector<std::pair<DataType, llvm::Value *>> ownedManagedArguments;
	size_t runtimeArgumentStart = externalCallRuntimeArgumentStart(kind);
	for (size_t i = runtimeArgumentStart; i < args.size(); ++i) {
		DataType argumentType = finalizedExpressionType(context, args[i]);
		if (args[i]->kind == Expression::Kind::Literal) {
			if (auto *str = std::get_if<std::string>(&args[i]->literalValue)) {
				std::string globalName = ".str." + std::to_string(context.stringConstants.size());
				llvm::Constant *strConst = llvm::ConstantDataArray::getString(*context.llvmContext, *str, true);
				llvm::GlobalVariable *strGlobal = new llvm::GlobalVariable(
					*context.llvmModule, strConst->getType(), true, llvm::GlobalValue::PrivateLinkage, strConst, globalName
				);
				context.stringConstants[*str] = strGlobal;
				callArgs.push_back(builder.CreateInBoundsGEP(
					strGlobal->getValueType(), strGlobal, {builder.getInt64(0), builder.getInt64(0)}, "str_ptr"
				));
				callArgumentTypes.push_back(argumentType);
				continue;
			}
		}
		llvm::Value *argVal = nullptr;
		if (!generateRuntimeValue(args[i], argVal))
			return CodegenResult::failure();
		requireCompilerInvariant(argVal != nullptr, "external call argument produced no runtime value after inference");
		callArgs.push_back(argVal);
		callArgumentTypes.push_back(argumentType);
		if (typeHasManagedLifecycle(argumentType) && managedExpressionResultIsOwned(context, args[i]))
			ownedManagedArguments.push_back({argumentType, argVal});
	}

	size_t fixedArgumentCount = callArgs.size();
	bool isVariadic = kind == IntrinsicKind::VariadicCall;
	if (isVariadic) {
		int inferredFixedArgumentCount = 0;
		requireCompilerInvariant(
			resolveStoredCompileTimeInteger(args[4], {}, inferredFixedArgumentCount),
			"variadic call reached codegen without its inferred fixed argument count"
		);
		requireCompilerInvariant(
			inferredFixedArgumentCount >= 0 && static_cast<size_t>(inferredFixedArgumentCount) <= callArgs.size(),
			"variadic call reached codegen with an invalid fixed argument count"
		);
		fixedArgumentCount = static_cast<size_t>(inferredFixedArgumentCount);
		for (size_t argumentIndex = fixedArgumentCount; argumentIndex < callArgs.size(); argumentIndex++) {
			DataType &argumentType = callArgumentTypes[argumentIndex];
			DataType promotedType = argumentType;
			if (!argumentType.isPointer()) {
				if (argumentType.kind == DataType::Kind::Float && argumentType.numericSize < 8)
					promotedType = {DataType::Kind::Float, 8};
				else if (argumentType.kind == DataType::Kind::Bool ||
						 (argumentType.kind == DataType::Kind::Int && argumentType.numericSize < 4))
					promotedType = {DataType::Kind::Int, 4};
			}
			if (promotedType != argumentType) {
				callArgs[argumentIndex] = ensureType(context, callArgs[argumentIndex], argumentType, promotedType);
				argumentType = promotedType;
			}
		}
	}
	std::vector<llvm::Type *> fixedArgumentTypes;
	fixedArgumentTypes.reserve(fixedArgumentCount);
	for (size_t argumentIndex = 0; argumentIndex < fixedArgumentCount; argumentIndex++)
		fixedArgumentTypes.push_back(callArgs[argumentIndex]->getType());
	llvm::FunctionType *funcType = llvm::FunctionType::get(returnLLVMType, fixedArgumentTypes, isVariadic);
	llvm::FunctionCallee callee = context.llvmModule->getOrInsertFunction(funcName, funcType);

	llvm::Value *callResult = builder.CreateCall(callee, callArgs);
	for (auto iterator = ownedManagedArguments.rbegin(); iterator != ownedManagedArguments.rend(); iterator++)
		if (!releaseManagedValue(context, iterator->first, iterator->second))
			return CodegenResult::failure();
	// If return type is void, return nullptr (no value to use)
	if (returnType.kind == DataType::Kind::Void)
		return nullptr;
	return callResult;
}

if (kind == IntrinsicKind::Function) {
	requireCompilerInvariant(callExpr, "function intrinsic reached codegen without its call expression");
	PatternDefinition *definition = callExpr->selectedCallableDefinition;
	requireCompilerInvariant(definition, "function intrinsic reached codegen without its inferred callable definition");
	requireCompilerInvariant(
		callExpr->selectedInstantiation == definition->callableInstantiation,
		"function intrinsic callable selection diverged after inference"
	);
	llvm::Function *callableFunction = nullptr;
	if (!ensureCallableFunctionGenerated(context, definition, definition->section->isExposed, callableFunction))
		return CodegenResult::failure();
	if (callableFunction->getType() == builder.getPtrTy())
		return callableFunction;
	return builder.CreateBitCast(callableFunction, builder.getPtrTy(), "function_ptr");
}

if (kind == IntrinsicKind::Cast) {
	// Format: args[1]=value, args[2]=type (TypeReference)
	llvm::Value *val = nullptr;
	if (!generateRuntimeValue(args[1], val))
		return CodegenResult::failure();
	DataType fromType = finalizedExpressionType(context, args[1]);

	// Get target type from the TypeReference argument
	DataType typeArgType = finalizedExpressionType(context, args[2]);
	if (typeArgType.kind != DataType::Kind::Type)
		return nullptr; // unresolved type (e.g. spurious top-level instantiation)
	DataType toType = typeArgType.toReferencedType();

	return ensureType(context, val, fromType, toType);
}

if (kind == IntrinsicKind::Type || kind == IntrinsicKind::Fix) {
	// Meta-values are compile-time only — no runtime code.
	return nullptr;
}

if (kind == IntrinsicKind::SizeOf) {
	DataType typeArgType = finalizedExpressionType(context, args[1]);
	if (typeArgType.kind != DataType::Kind::Type)
		return nullptr;
	DataType valueType = typeArgType.toReferencedType();
	requireCompilerInvariant(valueType.isConcrete(), "size of reached codegen with a non-concrete type");
	return builder.getInt64(valueType.getByteSize(context.llvmModule->getDataLayout(), *context.llvmContext));
}

if (kind == IntrinsicKind::BuildInfo || kind == IntrinsicKind::TargetIs || kind == IntrinsicKind::ShaderStageIs) {
	CompileTimeValue value = resolveStoredCompileTimeValue(callExpr, context.flexBindingFrames);
	if (auto *number = std::get_if<double>(&value)) {
		llvm::Type *llvmType = getLLVMType(context, resultType);
		return llvm::ConstantInt::get(llvmType, static_cast<std::int64_t>(*number), true);
	}
	if (auto *boolean = std::get_if<bool>(&value)) {
		llvm::Type *llvmType = getLLVMType(context, resultType);
		return llvm::ConstantInt::get(llvmType, *boolean ? 1 : 0, false);
	}
	return nullptr;
}

if (kind == IntrinsicKind::TypeOf || kind == IntrinsicKind::Array) {
	// Compile-time only — no runtime code
	return nullptr;
}

if (kind == IntrinsicKind::Select) {
	if (!args[1])
		crashCompilerBug("select intrinsic missing condition argument during codegen");
	CompileTimeValue conditionValue = resolveStoredCompileTimeValue(args[1], context.flexBindingFrames);
	auto *condition = std::get_if<bool>(&conditionValue);
	if (condition) {
		Expression *selectedExpression = args[*condition ? 2 : 3];
		llvm::Value *selectedValue = nullptr;
		if (!generateRuntimeValue(selectedExpression, selectedValue))
			return CodegenResult::failure();
		if (typeHasManagedLifecycle(resultType) && !managedExpressionResultIsOwned(context, selectedExpression) &&
			!retainManagedValue(context, resultType, selectedValue))
			return CodegenResult::failure();
		return selectedValue;
	}
	return buildRuntimeSelect(context, args, resultType);
}

if (kind == IntrinsicKind::AddPointerDepth) {
	// Compile-time only — modifies TypeReference, no runtime code
	return nullptr;
}

if (kind == IntrinsicKind::Construct) {
	if (resultType.kind == DataType::Kind::Array) {
		llvm::Type *arrayType = getLLVMType(context, resultType);
		llvm::AllocaInst *alloca = createEntryAlloca(context, "array_tmp", resultType);
		DataType elementType = *resultType.arrayElementType;
		for (size_t i = 2; i < args.size(); i++) {
			llvm::Value *elementVal = nullptr;
			if (!generateRuntimeValue(args[i], elementVal))
				return CodegenResult::failure();
			DataType fromType = finalizedExpressionType(context, args[i]);
			elementVal = ensureType(context, elementVal, fromType, elementType);
			if (typeHasManagedLifecycle(elementType) && !managedExpressionResultIsOwned(context, args[i]) &&
				!retainManagedValue(context, elementType, elementVal))
				return CodegenResult::failure();
			llvm::Value *elementPtr =
				builder.CreateGEP(arrayType, alloca, {builder.getInt64(0), builder.getInt64(i - 2)}, "array_elem_ptr");
			builder.CreateStore(elementVal, elementPtr);
		}
		return builder.CreateAlignedLoad(arrayType, alloca, getLLVMABIAlignment(context, resultType), "array_load");
	}

	if (resultType.kind == DataType::Kind::Vector) {
		return buildVectorValue(context, resultType, args, 2);
	}

	if (resultType.kind == DataType::Kind::Matrix) {
		if (args.size() == 3)
			return buildMatrixFromFlatArray(context, resultType, args[2]);
		return buildMatrixFromScalarArgs(context, resultType, args, 2);
	}

	if (resultType.kind != DataType::Kind::Class) {
		llvm::Value *val = nullptr;
		if (!generateRuntimeValue(args[2], val))
			return CodegenResult::failure();
		DataType fromType = finalizedExpressionType(context, args[2]);
		return ensureType(context, val, fromType, resultType);
	}

	ClassDefinition *classDef = resultType.classDefinition;
	DataType concreteType = resultType;
	if (concreteType.classInstIndex == -1) {
		std::vector<DataType> fieldTypes;
		fieldTypes.reserve(args.size() - 2);
		for (size_t i = 2; i < args.size(); i++)
			fieldTypes.push_back(finalizedExpressionType(context, args[i]));
		concreteType.classInstIndex = classDef->getOrCreateInstantiation(fieldTypes);
	}
	requireCompilerInvariant(
		concreteType.classInstIndex >= 0, "class construct result type must have a concrete instantiation"
	);
	ClassInstantiation &inst = classDef->instantiations[concreteType.classInstIndex];
	llvm::AllocaInst *alloca = createEntryAlloca(context, "class_tmp", concreteType);
	std::vector<std::pair<DataType, llvm::Value *>> ownedManagedFields;

	for (size_t i = 0; i < inst.fieldTypes.size(); i++) {
		llvm::Value *fieldVal = nullptr;
		if (!generateRuntimeValue(args[i + 2], fieldVal))
			return CodegenResult::failure();
		DataType fieldFromType = finalizedExpressionType(context, args[i + 2]);
		fieldVal = ensureType(context, fieldVal, fieldFromType, inst.fieldTypes[i]);
		if (typeHasManagedLifecycle(inst.fieldTypes[i])) {
			if (!managedExpressionResultIsOwned(context, args[i + 2]) &&
				!retainManagedValue(context, inst.fieldTypes[i], fieldVal))
				return CodegenResult::failure();
			ownedManagedFields.push_back({inst.fieldTypes[i], fieldVal});
		}
		llvm::Value *fieldPtr = createClassFieldPointer(context, concreteType, static_cast<int>(i), alloca);
		builder.CreateStore(fieldVal, fieldPtr);
	}

	llvm::Value *result = builder.CreateAlignedLoad(
		getLLVMType(context, concreteType), alloca, getLLVMABIAlignment(context, concreteType), "struct_load"
	);
	if (classDef->retainSection) {
		if (!retainManagedValue(context, concreteType, result))
			return CodegenResult::failure();
		for (auto iterator = ownedManagedFields.rbegin(); iterator != ownedManagedFields.rend(); iterator++)
			if (!releaseManagedValue(context, iterator->first, iterator->second))
				return CodegenResult::failure();
	}
	return result;
}

if (kind == IntrinsicKind::Property) {
	// Format: args[1]=instance, args[2]=fieldname (string literal from {word:} capture)
	Expression *ownerExpr = args[1];
	DataType ownerType = finalizedExpressionType(context, ownerExpr);
	bool ownerIsDirectClassPointer = ownerType.kind == DataType::Kind::Class && ownerType.pointerDepth == 1;
	DataType instType = ownerIsDirectClassPointer ? ownerType.dereferenced() : ownerType;
	ClassDefinition *classDef = instType.isPointer() ? nullptr : instType.classDefinition;

	// Get field name from string literal
	std::string fieldName;
	CompileTimeValue propertyValue = resolveStoredCompileTimeValue(args[2], context.flexBindingFrames);
	if (const auto *propertyName = std::get_if<std::string>(&propertyValue))
		fieldName = *propertyName;
	if (fieldName.empty()) {
		Expression *propExpr = resolveVariableBinding(context, args[2]);
		fieldName = getStringLiteral(propExpr);
	}

	// C strings expose a synthetic "data" property so string-library flexes can
	// operate on both heap strings and string literals without duplicating logic.
	if (fieldName == "data" && instType.isBytePointer())
		return generateExpressionCode(context, ownerExpr);

	if (!classDef) {
		context.diagnostics.push_back(Diagnostic(
			context, Diagnostic::Level::Error, "class has no properties", args[1]->range, "type", instType.toString()
		));
		return CodegenResult::failure();
	}

	// Find field index
	int fieldIdx = -1;
	for (size_t i = 0; i < classDef->fields.size(); i++) {
		if (classDef->fields[i].name == fieldName) {
			fieldIdx = i;
			break;
		}
	}

	if (fieldIdx == -1) {
		context.diagnostics.push_back(Diagnostic(
			context, Diagnostic::Level::Error, "class missing property", args[1]->range, "type", instType.toString(),
			"property", fieldName
		));
		return CodegenResult::failure();
	}

	DataType fieldType = classDef->instantiations[instType.classInstIndex].fieldTypes[fieldIdx];
	LValueAddressResult propertyAddress = generateLValueAddress(context, callExpr);
	if (propertyAddress.status == LValueAddressStatus::Failed)
		return CodegenResult::failure();
	if (propertyAddress.status == LValueAddressStatus::Addressable) {
		requireCompilerInvariant(propertyAddress.address != nullptr, "addressable property produced no field address");
		llvm::Value *fieldValue = builder.CreateAlignedLoad(
			getLLVMType(context, fieldType), propertyAddress.address, getLLVMABIAlignment(context, fieldType),
			fieldName + "_val"
		);
		if (typeHasManagedLifecycle(fieldType) && !retainManagedValue(context, fieldType, fieldValue))
			return CodegenResult::failure();
		return fieldValue;
	}

	llvm::Value *instValue = nullptr;
	if (!generateRuntimeValue(ownerExpr, instValue))
		return CodegenResult::failure();
	requireCompilerInvariant(instValue != nullptr, "class property rvalue owner produced no value");
	llvm::Value *fieldValue =
		builder.CreateExtractValue(instValue, {getClassFieldLLVMIndex(context, instType, fieldIdx)}, fieldName + "_val");
	if (typeHasManagedLifecycle(fieldType) && !retainManagedValue(context, fieldType, fieldValue))
		return CodegenResult::failure();
	if (typeHasManagedLifecycle(instType) && managedExpressionResultIsOwned(context, ownerExpr) &&
		!releaseManagedValue(context, instType, instValue))
		return CodegenResult::failure();
	return fieldValue;
}

// Shader I/O intrinsics (only available in --emit-spirv mode)
if (kind == IntrinsicKind::ShaderInput) {
	// @intrinsic("shader input", globalName) → load vec4 from named shader input global
	std::string inputName = getStringLiteral(args[1]);
	std::string globalName;
	if (inputName == "FragCoord")
		globalName = "gl_FragCoord";
	else if (inputName == "Position")
		globalName = "in_Position";
	else {
		context.diagnostics.push_back(
			Diagnostic(context, Diagnostic::Level::Error, "unknown shader input", Range(), "name", inputName)
		);
		return CodegenResult::failure();
	}
	llvm::GlobalVariable *global = context.llvmModule->getGlobalVariable(globalName);
	if (!global) {
		context.diagnostics.push_back(
			Diagnostic(context, Diagnostic::Level::Error, "shader input unavailable", args[1]->range, "name", inputName)
		);
		return CodegenResult::failure();
	}
	llvm::Type *vec4Ty = llvm::FixedVectorType::get(builder.getFloatTy(), 4);
	return builder.CreateLoad(vec4Ty, global, inputName);
}

if (kind == IntrinsicKind::ShaderUniform) {
	// @intrinsic("shader uniform", uniformName) → load f32 from named uniform global
	// The SPIR-V patcher wraps this in a UBO struct with proper decorations
	std::string uniformName = getStringLiteral(args[1]);
	if (uniformName.empty()) {
		context.addDiagnostic(Diagnostic(
			context, Diagnostic::Level::Error, "shader uniform requires string literal", args[1] ? args[1]->range : Range()
		));
		return CodegenResult::failure();
	}
	std::string globalName = "ubo_" + uniformName;
	llvm::GlobalVariable *global = context.llvmModule->getGlobalVariable(globalName);
	if (!global) {
		llvm::Type *f32Ty = builder.getFloatTy();
		global = new llvm::GlobalVariable(
			*context.llvmModule, f32Ty, false, llvm::GlobalValue::ExternalLinkage, nullptr, globalName, nullptr,
			llvm::GlobalValue::NotThreadLocal, 3
		);
		global->setInitializer(llvm::Constant::getNullValue(f32Ty));
		context.registerShaderUniformName(uniformName);
	}
	return builder.CreateLoad(builder.getFloatTy(), global, uniformName + "_val");
}

if (kind == IntrinsicKind::ShaderOutput) {
	// @intrinsic("shader output", r, g, b, a) → store vec4 to shader output global
	llvm::Value *r = nullptr;
	llvm::Value *g = nullptr;
	llvm::Value *b = nullptr;
	llvm::Value *a = nullptr;
	if (!generateRuntimeValue(args[1], r) || !generateRuntimeValue(args[2], g) || !generateRuntimeValue(args[3], b) ||
		!generateRuntimeValue(args[4], a))
		return CodegenResult::failure();

	DataType rType = finalizedExpressionType(context, args[1]);
	DataType gType = finalizedExpressionType(context, args[2]);
	DataType bType = finalizedExpressionType(context, args[3]);
	DataType aType = finalizedExpressionType(context, args[4]);
	DataType f32 = {DataType::Kind::Float, 4};
	r = ensureType(context, r, rType, f32);
	g = ensureType(context, g, gType, f32);
	b = ensureType(context, b, bType, f32);
	a = ensureType(context, a, aType, f32);

	llvm::Type *vec4Ty = llvm::FixedVectorType::get(builder.getFloatTy(), 4);
	llvm::Value *color = llvm::UndefValue::get(vec4Ty);
	color = builder.CreateInsertElement(color, r, getVectorLaneIndexValue(context, 0), "color_r");
	color = builder.CreateInsertElement(color, g, getVectorLaneIndexValue(context, 1), "color_g");
	color = builder.CreateInsertElement(color, b, getVectorLaneIndexValue(context, 2), "color_b");
	color = builder.CreateInsertElement(color, a, getVectorLaneIndexValue(context, 3), "color_a");

	// Find the output global: gl_FragColor (fragment) or gl_Position (vertex)
	std::string outName = (context.options.shaderStage == ParseContext::ShaderStage::Vertex) ? "gl_Position" : "gl_FragColor";
	llvm::GlobalVariable *outGlobal = context.llvmModule->getGlobalVariable(outName);
	if (!outGlobal) {
		context.diagnostics.push_back(Diagnostic(
			context, Diagnostic::Level::Error, "shader output unavailable",
			Range(args[1]->range.line, args[1]->range.start(), args[4]->range.end())
		));
		return CodegenResult::failure();
	}
	builder.CreateStore(color, outGlobal);
	return nullptr;
}

if (kind == IntrinsicKind::ExtractElement) {
	// @intrinsic("extract element", vector, index) → extract scalar from vector
	llvm::Value *vec = nullptr;
	if (!generateRuntimeValue(args[1], vec))
		return CodegenResult::failure();
	if (auto *idxLit = std::get_if<double>(&args[2]->literalValue)) {
		return builder.CreateExtractElement(vec, getVectorLaneIndexValue(context, static_cast<unsigned>(*idxLit)), "elem");
	}
	llvm::Value *idx = nullptr;
	if (!generateRuntimeValue(args[2], idx))
		return CodegenResult::failure();
	idx = ensureType(context, idx, finalizedExpressionType(context, args[2]), {DataType::Kind::Int, 4});
	return builder.CreateExtractElement(vec, idx, "elem");
}

std::string uri =
	(callExpr && callExpr->range.line && callExpr->range.line->sourceFile) ? callExpr->range.line->sourceFile->uri : "";
int line = (callExpr && callExpr->range.line) ? callExpr->range.line->sourceFileLineIndex + 1 : -1;
crashUnimplementedIntrinsic("codegen", name, uri, line);
}
