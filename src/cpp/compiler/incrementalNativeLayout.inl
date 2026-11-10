// Partition constraints belong to LLVM symbols, independently of DynLex syntax.
class NativePartitionLayout {
	std::unordered_map<const llvm::GlobalValue *, const llvm::GlobalValue *> parents;
	std::unordered_map<const llvm::GlobalValue *, unsigned> partitions;
	std::unordered_map<const llvm::GlobalValue *, std::string> components;

	const llvm::GlobalValue *root(const llvm::GlobalValue *value) {
		auto &parent = parents.at(value);
		if (parent != value)
			parent = root(parent);
		return parent;
	}
	void join(const llvm::GlobalValue *left, const llvm::GlobalValue *right) {
		left = root(left);
		right = root(right);
		if (left->getName() > right->getName())
			std::swap(left, right);
		parents.at(right) = left;
	}

  public:
	explicit NativePartitionLayout(llvm::Module &module) {
		for (const llvm::GlobalValue &value : module.global_values())
			if (!value.isDeclaration())
				parents.emplace(&value, &value);
		std::unordered_map<const llvm::Comdat *, const llvm::GlobalValue *> comdats;
		for (const auto &[value, _] : parents) {
			if (const llvm::Comdat *comdat = value->getComdat()) {
				auto [member, inserted] = comdats.emplace(comdat, value);
				if (!inserted)
					join(value, member->second);
			}
			const llvm::GlobalObject *aliasee = value->getAliaseeObject();
			if (const auto *ifunc = llvm::dyn_cast_or_null<llvm::GlobalIFunc>(aliasee))
				aliasee = ifunc->getResolverFunction();
			if (aliasee && value != aliasee)
				join(value, aliasee);
		}
		// A block address requires its function body and all its users to remain
		// together, even after their linkage has been externalized.
		for (const llvm::Function &function : module) {
			for (const llvm::BasicBlock &block : function) {
				const llvm::BlockAddress *address = llvm::BlockAddress::lookup(&block);
				if (!address || !address->isConstantUsed())
					continue;
				std::vector<const llvm::User *> pending(address->user_begin(), address->user_end());
				std::unordered_set<const llvm::User *> visited;
				while (!pending.empty()) {
					const llvm::User *user = pending.back();
					pending.pop_back();
					if (!visited.insert(user).second)
						continue;
					if (const auto *owner = llvm::dyn_cast<llvm::GlobalValue>(user))
						join(&function, owner);
					else if (const auto *instruction = llvm::dyn_cast<llvm::Instruction>(user))
						join(&function, instruction->getFunction());
					else if (llvm::isa<llvm::Constant>(user))
						pending.insert(pending.end(), user->user_begin(), user->user_end());
					else
						crashCompilerBug("native block address has no containing symbol");
				}
			}
		}
		for (const auto &[value, _] : parents) {
			components.emplace(value, root(value)->getName().str());
			std::string hash = digest(root(value)->getName().str());
			unsigned number = 0;
			for (char digit : std::string_view(hash).substr(0, 8))
				number = number * 16 + (digit <= '9' ? digit - '0' : digit - 'a' + 10);
			partitions.emplace(value, number % partitionCount);
		}
	}

	unsigned partition(const llvm::GlobalValue &value) const { return partitions.at(&value); }
	const std::string &component(const llvm::GlobalValue &value) const { return components.at(&value); }
};
