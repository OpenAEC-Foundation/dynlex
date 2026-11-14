// Static data stays a module constant. Runtime initializers execute in their
// declaration's lexical scope and transfer their snapshot into owning storage.
// Readers never share instructions belonging to a different LLVM function.
using DataInstructionSnapshot = std::unordered_set<const llvm::Instruction *>;

static DataInstructionSnapshot currentDataInstructions(ParseContext &context) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	DataInstructionSnapshot instructions;
	for (const auto &block : *builder.GetInsertBlock()->getParent())
		for (const auto &instruction : block)
			instructions.insert(&instruction);
	return instructions;
}

static bool dataInitializerHasEffects(ParseContext &context, const DataInstructionSnapshot &preceding) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	for (const auto &block : *builder.GetInsertBlock()->getParent()) {
		for (const auto &instruction : block) {
			if (preceding.contains(&instruction) || !instruction.mayHaveSideEffects())
				continue;
			// Folded pure calls still prepare their private argument slots.
			// Writes to previously existing storage remain observable effects.
			if (const auto *store = llvm::dyn_cast<llvm::StoreInst>(&instruction)) {
				const auto *slot = llvm::dyn_cast<llvm::AllocaInst>(store->getPointerOperand()->stripPointerCasts());
				if (slot && !preceding.contains(slot) && !store->isVolatile() && !store->isAtomic())
					continue;
			}
			return true;
		}
	}
	return false;
}

static llvm::GlobalVariable *globalDataStorage(ParseContext &context, Variable *variable, const DataType &type) {
	auto found = context.globalLLVMVariables.find(variable->name);
	if (found != context.globalLLVMVariables.end())
		return found->second;
	auto *storage = new llvm::GlobalVariable(
		*context.llvmModule, getLLVMType(context, type), false, llvm::GlobalValue::InternalLinkage,
		llvm::Constant::getNullValue(getLLVMType(context, type)), variable->name
	);
	storage->setAlignment(getLLVMABIAlignment(context, type));
	context.globalLLVMVariables[variable->name] = storage;
	if (typeHasManagedLifecycle(type))
		registerManagedGlobalStorage(context, storage, type);
	return storage;
}

static CodegenResult loadGlobalDataValue(ParseContext &context, Variable *variable, const DataType &type, bool owningRead) {
	if (type.isMetaType())
		return nullptr;
	llvm::Value *value = loadLLVMValue(context, type, globalDataStorage(context, variable, type), "data.value");
	if (owningRead && typeHasManagedLifecycle(type) && !retainManagedValue(context, type, value))
		return CodegenResult::failure();
	return value;
}

static bool storeGlobalDataValue(
	ParseContext &context, Expression *expression, Variable *variable, llvm::Value *value,
	const DataInstructionSnapshot &preceding
) {
	DataType type = finalizedExpressionType(context, expression);
	if (type.isMetaType())
		return true;
	auto *storage = globalDataStorage(context, variable, type);
	if (auto *constant = llvm::dyn_cast<llvm::Constant>(value);
		constant && !typeHasManagedLifecycle(type) && !dataInitializerHasEffects(context, preceding)) {
		storage->setInitializer(constant);
		storage->setConstant(true);
		context.dataLLVMValues[expression] = constant;
		return true;
	}
	if (typeHasManagedLifecycle(type))
		return storeManagedValue(context, storage, type, value);
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	builder.CreateAlignedStore(value, storage, getLLVMABIAlignment(context, type));
	return true;
}

static CodegenResult loadLocalDataValue(ParseContext &context, Expression *expression, bool owningRead) {
	DataType type = finalizedExpressionType(context, expression);
	if (type.isMetaType())
		return nullptr;
	llvm::Value *cached = context.localDataLLVMValues.at(expression);
	llvm::Value *value = llvm::isa<llvm::Constant>(cached) ? cached : loadLLVMValue(context, type, cached, "data.value");
	if (owningRead && typeHasManagedLifecycle(type) && !retainManagedValue(context, type, value))
		return CodegenResult::failure();
	return value;
}

static bool storeLocalDataValue(ParseContext &context, Expression *expression, llvm::Value *value) {
	DataType type = finalizedExpressionType(context, expression);
	if (type.isMetaType()) {
		context.localDataLLVMValues[expression] = nullptr;
		return true;
	}
	if (llvm::isa<llvm::Constant>(value) && !typeHasManagedLifecycle(type)) {
		context.localDataLLVMValues[expression] = value;
		return true;
	}
	Expression *declaration = expression->dataDeclaration;
	auto *storage = createEntryAlloca(context, declaration->variable->name, type);
	context.localDataLLVMValues[expression] = storage;
	if (typeHasManagedLifecycle(type)) {
		registerManagedStorage(context, storage, type, declaration->range.section());
		return storeManagedValue(context, storage, type, value);
	}
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	builder.CreateAlignedStore(value, storage, getLLVMABIAlignment(context, type));
	return true;
}
