class NativeSymbolReferences {
	std::unordered_set<const llvm::Value *> values;
	std::unordered_set<const llvm::Metadata *> metadata;
	std::vector<const llvm::GlobalValue *> globals;

	void visitMetadata(const llvm::Metadata *item) {
		if (!item || !metadata.insert(item).second)
			return;
		if (const auto *value = llvm::dyn_cast<llvm::ValueAsMetadata>(item))
			visit(value->getValue());
		else if (const auto *node = llvm::dyn_cast<llvm::MDNode>(item))
			for (const llvm::MDOperand &operand : node->operands())
				visitMetadata(operand.get());
	}
	void visit(const llvm::Value *value) {
		if (!values.insert(value).second)
			return;
		if (const auto *global = llvm::dyn_cast<llvm::GlobalValue>(value)) {
			globals.push_back(global);
			return;
		}
		if (const auto *user = llvm::dyn_cast<llvm::User>(value))
			for (const llvm::Use &operand : user->operands())
				visit(operand.get());
	}
	template <typename T> void visitAttachments(const T &value) {
		llvm::SmallVector<std::pair<unsigned, llvm::MDNode *>, 4> attachments;
		value.getAllMetadata(attachments);
		for (const auto &[_, node] : attachments)
			visitMetadata(node);
	}

  public:
	std::vector<const llvm::GlobalValue *> collectGlobals(const llvm::GlobalValue &value) {
		for (const llvm::Use &operand : value.operands())
			visit(operand.get());
		if (const auto *object = llvm::dyn_cast<llvm::GlobalObject>(&value))
			visitAttachments(*object);
		if (const auto *function = llvm::dyn_cast<llvm::Function>(&value))
			for (const llvm::BasicBlock &block : *function)
				for (const llvm::Instruction &instruction : block) {
					for (const llvm::Use &operand : instruction.operands())
						visit(operand.get());
					visitAttachments(instruction);
				}
		return std::move(globals);
	}
	std::set<std::string> collect(const llvm::GlobalValue &value) {
		std::set<std::string> names;
		for (const llvm::GlobalValue *global : collectGlobals(value))
			names.insert(global->getName().str());
		return names;
	}
};

// A standalone symbol module gives attributes, metadata and recursive named
// types their own stable numbering, without hashing unrelated declarations.
std::string nativeSymbolFingerprint(const llvm::GlobalValue &value, bool definition) {
	llvm::Module isolated("native symbol", value.getContext());
	llvm::GlobalValue *copy;
	if (const auto *function = llvm::dyn_cast<llvm::Function>(&value)) {
		copy = llvm::Function::Create(
			function->getFunctionType(), function->getLinkage(), function->getAddressSpace(), function->getName(), &isolated
		);
		auto *destination = llvm::cast<llvm::Function>(copy);
		if (definition) {
			llvm::ValueToValueMapTy values;
			values[function] = destination;
			auto argument = destination->arg_begin();
			for (const llvm::Argument &source : function->args()) {
				argument->setName(source.getName());
				values[&source] = &*argument++;
			}
			llvm::SmallVector<llvm::ReturnInst *, 4> returns;
			llvm::CloneFunctionInto(destination, function, values, llvm::CloneFunctionChangeType::ClonedModule, returns);
		} else
			destination->copyAttributesFrom(function);
	} else if (const auto *global = llvm::dyn_cast<llvm::GlobalVariable>(&value)) {
		copy = new llvm::GlobalVariable(
			isolated, global->getValueType(), global->isConstant(), global->getLinkage(),
			definition && global->hasInitializer() ? const_cast<llvm::Constant *>(global->getInitializer()) : nullptr,
			global->getName(), nullptr, global->getThreadLocalMode(), global->getAddressSpace(),
			global->isExternallyInitialized()
		);
		llvm::cast<llvm::GlobalVariable>(copy)->copyAttributesFrom(global);
	} else if (const auto *alias = llvm::dyn_cast<llvm::GlobalAlias>(&value)) {
		copy = llvm::GlobalAlias::create(
			alias->getValueType(), alias->getAddressSpace(), alias->getLinkage(), alias->getName(),
			const_cast<llvm::Constant *>(alias->getAliasee()), &isolated
		);
		llvm::cast<llvm::GlobalAlias>(copy)->copyAttributesFrom(alias);
	} else {
		const auto &ifunc = llvm::cast<llvm::GlobalIFunc>(value);
		copy = llvm::GlobalIFunc::create(
			ifunc.getValueType(), ifunc.getAddressSpace(), ifunc.getLinkage(), ifunc.getName(),
			const_cast<llvm::Constant *>(ifunc.getResolver()), &isolated
		);
		llvm::cast<llvm::GlobalIFunc>(copy)->copyAttributesFrom(&ifunc);
	}
	if (const auto *object = llvm::dyn_cast<llvm::GlobalObject>(&value)) {
		auto *destination = llvm::cast<llvm::GlobalObject>(copy);
		if (!definition || !llvm::isa<llvm::Function>(value))
			destination->copyMetadata(object, 0);
		if (const llvm::Comdat *comdat = object->getComdat()) {
			llvm::Comdat *group = isolated.getOrInsertComdat(comdat->getName());
			group->setSelectionKind(comdat->getSelectionKind());
			destination->setComdat(group);
		}
	}
	return digest(renderModule(isolated));
}
