// Conditional operands share one branch-emission path, including ownership and
// conversions. Boolean and/or are selections with one literal Boolean branch.
static CodegenResult buildConditionalValue(
	ParseContext &context, const std::vector<Expression *> &args, DataType resultType, ConditionalOperands operands
) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	requireCompilerInvariant(builder.GetInsertBlock(), "conditional codegen has no insertion block");
	llvm::Function *function = builder.GetInsertBlock()->getParent();
	requireCompilerInvariant(
		finalizedExpressionType(context, args[1]).kind == DataType::Kind::Bool,
		"conditional operand must be Boolean after inference"
	);
	CodegenResult condition = generateExpressionCode(context, args[1]);
	if (!condition)
		return condition;
	requireCompilerInvariant(condition.value, "conditional operand produced no value");
	if (resultType.isMetaType()) {
		context.diagnostics.push_back(
			Diagnostic(context, Diagnostic::Level::Error, "compile time type value used at runtime", args[1]->range)
		);
		return CodegenResult::failure();
	}
	auto generateBranch = [&](bool selected) -> CodegenResult {
		size_t index = operands.selected(selected);
		if (!index)
			return builder.getInt1(selected);
		Expression *expression = args[index];
		CodegenResult generated = generateExpressionCode(context, expression);
		if (!generated || builder.GetInsertBlock()->hasTerminator() || resultType.kind == DataType::Kind::Void)
			return generated;
		llvm::Value *value = ensureType(context, generated.value, finalizedExpressionType(context, expression), resultType);
		if (typeHasManagedLifecycle(resultType) && !managedExpressionResultIsOwned(context, expression) &&
			!retainManagedValue(context, resultType, value))
			return CodegenResult::failure();
		return value;
	};
	if (auto *constantCondition = llvm::dyn_cast<llvm::ConstantInt>(condition.value))
		return generateBranch(!constantCondition->isZero());
	llvm::BasicBlock *trueBlock = llvm::BasicBlock::Create(*context.llvmContext, "select.true", function);
	llvm::BasicBlock *falseBlock = llvm::BasicBlock::Create(*context.llvmContext, "select.false", function);
	llvm::BasicBlock *mergeBlock = llvm::BasicBlock::Create(*context.llvmContext, "select.merge", function);
	builder.CreateCondBr(condition.value, trueBlock, falseBlock);
	struct Incoming {
		llvm::Value *value;
		llvm::BasicBlock *block;
	};
	std::vector<Incoming> incoming;
	for (bool selected : {true, false}) {
		builder.SetInsertPoint(selected ? trueBlock : falseBlock);
		CodegenResult branch = generateBranch(selected);
		if (!branch)
			return branch;
		llvm::BasicBlock *end = builder.GetInsertBlock();
		if (!end->hasTerminator()) {
			if (resultType.kind != DataType::Kind::Void)
				incoming.push_back({branch.value, end});
			builder.CreateBr(mergeBlock);
		}
	}
	builder.SetInsertPoint(mergeBlock);
	if (resultType.kind == DataType::Kind::Void || incoming.empty())
		return nullptr;
	auto *phi = builder.CreatePHI(getLLVMType(context, resultType), static_cast<unsigned>(incoming.size()), "select");
	for (const auto &branch : incoming)
		phi->addIncoming(branch.value, branch.block);
	return phi;
}
