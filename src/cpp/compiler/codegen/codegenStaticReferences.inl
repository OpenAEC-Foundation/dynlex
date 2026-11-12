// A static object's identity follows its first use in the ordered IR graph.
// Module construction order differs when some function bodies come from cache.
// Naming by value would merge distinct objects whose addresses are observable.
static void stabilizeStaticReferenceIdentities(llvm::Module &module) {
	std::unordered_set<llvm::GlobalVariable *> statics;
	std::vector<llvm::GlobalValue *> roots;
	for (llvm::GlobalValue &value : module.global_values()) {
		auto *global = llvm::dyn_cast<llvm::GlobalVariable>(&value);
		if (global && global->getMetadata("dynlex.static.reference")) {
			requireCompilerInvariant(
				global->isConstant() && global->hasLocalLinkage() && global->hasInitializer(),
				"static reference has no private constant definition"
			);
			statics.insert(global);
		} else
			roots.push_back(&value);
	}
	if (statics.empty())
		return;
	std::ranges::sort(roots, {}, [](const llvm::GlobalValue *value) {
		return value->getName();
	});
	std::unordered_set<const llvm::Constant *> visited;
	std::unordered_map<llvm::GlobalVariable *, std::string> identities;
	auto visit = [&](auto &&self, llvm::Value *value, const std::string &path) -> void {
		auto *constant = llvm::dyn_cast<llvm::Constant>(value);
		if (!constant)
			return;
		if (auto *global = llvm::dyn_cast<llvm::GlobalValue>(constant)) {
			auto *object = llvm::dyn_cast<llvm::GlobalVariable>(global);
			if (!object || !statics.contains(object) || identities.contains(object))
				return;
			identities.emplace(object, ".dynlex.static." + sectionIdentityDigest(path));
			self(self, object->getInitializer(), path + "/initializer");
			return;
		}
		if (!visited.insert(constant).second)
			return;
		for (unsigned index = 0; index < constant->getNumOperands(); ++index)
			self(self, constant->getOperand(index), path + "/" + std::to_string(index));
	};
	for (llvm::GlobalValue *root : roots) {
		std::string path = std::to_string(root->getName().size()) + ":" + root->getName().str();
		for (unsigned index = 0; index < root->getNumOperands(); ++index)
			visit(visit, root->getOperand(index), path + "/global/" + std::to_string(index));
		if (auto *function = llvm::dyn_cast<llvm::Function>(root)) {
			size_t instructionIndex = 0;
			for (llvm::BasicBlock &block : *function)
				for (llvm::Instruction &instruction : block) {
					std::string instructionPath = path + "/instruction/" + std::to_string(instructionIndex++);
					for (unsigned index = 0; index < instruction.getNumOperands(); ++index)
						visit(visit, instruction.getOperand(index), instructionPath + "/" + std::to_string(index));
				}
		}
	}
	// Private objects unreachable from every function and named global have no
	// observable address. Drop whole unreachable components, including cycles.
	std::vector<llvm::GlobalVariable *> unreachable;
	for (llvm::GlobalVariable *global : statics) {
		global->setName("");
		if (!identities.contains(global)) {
			global->dropAllReferences();
			unreachable.push_back(global);
		}
	}
	for (llvm::GlobalVariable *global : unreachable)
		global->eraseFromParent();
	for (const auto &[global, identity] : identities) {
		global->setName(identity);
		requireCompilerInvariant(global->getName() == identity, "static reference identities collide");
	}
}
