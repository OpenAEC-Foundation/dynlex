// Logical GPU addresses need no machine-sized integer. Reconstruct constant
// byte offsets as typed accesses, then use lossless 32-bit constant indices.
// LLVM's optimizer otherwise turns a matrix row into an i64 byte GEP, whose
// logical-address lowering unnecessarily requires ShaderInt64.
struct ShaderAddress {
	llvm::Type *element;
	llvm::Value *base;
	llvm::SmallVector<llvm::Value *, 8> indices;
};

static ShaderAddress shaderAddress(llvm::GEPOperator &gep, const llvm::DataLayout &layout) {
	ShaderAddress result{gep.getSourceElementType(), gep.getPointerOperand(), {}};
	for (auto &index : gep.indices())
		result.indices.push_back(index.get());
	llvm::Type *rootType = nullptr;
	if (auto *global = llvm::dyn_cast<llvm::GlobalVariable>(result.base))
		rootType = global->getValueType();
	else if (auto *allocation = llvm::dyn_cast<llvm::AllocaInst>(result.base))
		rootType = allocation->getAllocatedType();
	else if (auto *base = llvm::dyn_cast<llvm::GEPOperator>(result.base))
		rootType = base->getResultElementType();
	if (rootType && result.element->isIntegerTy(8)) {
		llvm::APInt offset(layout.getIndexSizeInBits(gep.getPointerAddressSpace()), 0);
		if (gep.accumulateConstantOffset(layout, offset)) {
			llvm::Type *element = rootType;
			auto indices = layout.getGEPIndicesForOffset(element, offset);
			if (offset.isZero()) {
				result.element = rootType;
				result.indices.clear();
				for (const auto &index : indices)
					result.indices.push_back(llvm::ConstantInt::get(gep.getContext(), index));
			}
		}
	}
	for (auto *&index : result.indices)
		if (auto *constant = llvm::dyn_cast<llvm::ConstantInt>(index))
			if (constant->getBitWidth() > 32 && constant->getValue().isSignedIntN(32))
				index = llvm::ConstantInt::get(gep.getContext(), constant->getValue().trunc(32));
	return result;
}

static llvm::Constant *shaderAddressConstant(
	llvm::Constant *value, const llvm::DataLayout &layout, std::unordered_map<llvm::Constant *, llvm::Constant *> &memo
) {
	auto *expression = llvm::dyn_cast<llvm::ConstantExpr>(value);
	if (!expression)
		return value;
	if (auto found = memo.find(value); found != memo.end())
		return found->second;
	llvm::SmallVector<llvm::Constant *, 8> operands;
	for (auto &operand : expression->operands())
		operands.push_back(shaderAddressConstant(llvm::cast<llvm::Constant>(operand), layout, memo));
	llvm::Constant *result = expression->getWithOperands(operands);
	if (auto *gep = llvm::dyn_cast<llvm::GEPOperator>(result)) {
		ShaderAddress address = shaderAddress(*gep, layout);
		llvm::SmallVector<llvm::Constant *, 8> indices;
		for (auto *index : address.indices)
			indices.push_back(llvm::cast<llvm::Constant>(index));
		result = llvm::ConstantExpr::getGetElementPtr(
			address.element, llvm::cast<llvm::Constant>(address.base), indices, gep->getNoWrapFlags()
		);
	}
	memo.emplace(value, result);
	return result;
}

static void lowerShaderAddressIndices(llvm::Module &module) {
	const llvm::DataLayout &layout = module.getDataLayout();
	std::unordered_map<llvm::Constant *, llvm::Constant *> memo;
	for (auto &function : module)
		for (auto &block : function)
			for (auto iterator = block.begin(); iterator != block.end();) {
				auto &instruction = *iterator++;
				for (auto &operand : instruction.operands())
					if (auto *constant = llvm::dyn_cast<llvm::ConstantExpr>(operand))
						operand = shaderAddressConstant(constant, layout, memo);
				auto *gep = llvm::dyn_cast<llvm::GetElementPtrInst>(&instruction);
				if (!gep)
					continue;
				ShaderAddress address = shaderAddress(*llvm::cast<llvm::GEPOperator>(gep), layout);
				auto *replacement = llvm::GetElementPtrInst::Create(
					address.element, address.base, address.indices, gep->getNoWrapFlags(), "shader_address", gep->getIterator()
				);
				gep->replaceAllUsesWith(replacement);
				gep->eraseFromParent();
			}
}
