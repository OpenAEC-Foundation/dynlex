struct ActiveClassInstantiation {
	ParseContext *parseContext{};
	ClassDefinition *classDefinition{};
	std::string requestKey;
	int symbolicIndex = -1;
};

struct ClassInstantiationTransaction {
	ParseContext *parseContext{};
	std::unordered_map<ClassDefinition *, size_t> originalInstantiationSizes;
	std::vector<std::pair<ClassDefinition *, std::string>> insertedRequestKeys;
	bool failed = false;
};

static thread_local std::vector<ActiveClassInstantiation> activeClassInstantiations;
static thread_local std::optional<ClassInstantiationTransaction> activeClassInstantiationTransaction;
static thread_local int nextSymbolicClassInstantiationIndex = -2;

static std::string buildClassInstantiationRequestKey(
	const BindingFrameStack &bindingFrameStack, InferenceContext *inferenceContext,
	const std::vector<DataType> *constructionArgumentTypes
) {
	std::vector<std::tuple<std::string, DataType, CompileTimeValue>> parameters;
	if (!bindingFrameStack.empty()) {
		for (const auto &[name, expression] : bindingFrameStack.topFrame().bindings) {
			DataType type = resolveKnownExpressionType(expression, bindingFrameStack, inferenceContext);
			CompileTimeValue value = resolveStoredCompileTimeValue(expression, bindingFrameStack, inferenceContext);
			parameters.push_back({name, std::move(type), std::move(value)});
		}
	}
	std::sort(parameters.begin(), parameters.end(), [](const auto &left, const auto &right) {
		return std::get<0>(left) < std::get<0>(right);
	});

	std::string key;
	for (const auto &[name, type, value] : parameters) {
		key += std::to_string(name.size()) + ":" + name;
		key += "=" + encodeDataTypeForCacheKey(type);
		key += ":" + encodeCompileTimeValueForCacheKey(value) + ";";
	}
	if (constructionArgumentTypes) {
		key += "#construct";
		for (const DataType &type : *constructionArgumentTypes)
			key += ":" + encodeDataTypeForCacheKey(type);
	}
	return key;
}

static void recordClassInstantiationAppend(ClassDefinition *classDefinition) {
	requireCompilerInvariant(
		activeClassInstantiationTransaction.has_value(), "class instantiation append has no active transaction"
	);
	activeClassInstantiationTransaction->originalInstantiationSizes.try_emplace(
		classDefinition, classDefinition->instantiations.size()
	);
}

static void cacheClassInstantiationRequest(ClassDefinition *classDefinition, const std::string &requestKey, int index) {
	auto [it, inserted] = classDefinition->instantiationIndicesByRequest.emplace(requestKey, index);
	if (!inserted) {
		requireCompilerInvariant(it->second == index, "class instantiation request resolved to inconsistent indices");
		return;
	}
	requireCompilerInvariant(
		activeClassInstantiationTransaction.has_value(), "class instantiation request cache has no active transaction"
	);
	activeClassInstantiationTransaction->insertedRequestKeys.push_back({classDefinition, requestKey});
}

static void rollbackClassInstantiationTransaction() {
	requireCompilerInvariant(
		activeClassInstantiationTransaction.has_value(), "cannot roll back a missing class instantiation transaction"
	);
	for (auto it = activeClassInstantiationTransaction->insertedRequestKeys.rbegin();
		 it != activeClassInstantiationTransaction->insertedRequestKeys.rend(); ++it) {
		it->first->instantiationIndicesByRequest.erase(it->second);
	}
	for (const auto &[classDefinition, originalSize] : activeClassInstantiationTransaction->originalInstantiationSizes) {
		requireCompilerInvariant(
			classDefinition->instantiations.size() >= originalSize,
			"class instantiation transaction observed an unexpected instantiation-list shrink"
		);
		classDefinition->instantiations.resize(originalSize);
	}
}

static bool
replaceSymbolicClassInstantiation(DataType &type, ClassDefinition *classDefinition, int symbolicIndex, int concreteIndex) {
	bool changed = false;
	if (type.classDefinition == classDefinition && type.classInstIndex == symbolicIndex) {
		type.classInstIndex = concreteIndex;
		changed = true;
	}
	if (type.arrayElementType)
		changed =
			replaceSymbolicClassInstantiation(*type.arrayElementType, classDefinition, symbolicIndex, concreteIndex) || changed;
	return changed;
}

static void resolveStoredSymbolicClassInstantiation(ClassDefinition *classDefinition, int symbolicIndex, int concreteIndex) {
	requireCompilerInvariant(
		activeClassInstantiationTransaction.has_value(), "symbolic class instantiation has no active transaction"
	);
	for (const auto &[mutatedClassDefinition, originalSize] : activeClassInstantiationTransaction->originalInstantiationSizes) {
		for (size_t index = originalSize; index < mutatedClassDefinition->instantiations.size(); index++) {
			for (DataType &fieldType : mutatedClassDefinition->instantiations[index].fieldTypes)
				replaceSymbolicClassInstantiation(fieldType, classDefinition, symbolicIndex, concreteIndex);
		}
	}
}

struct ScopedActiveClassInstantiation {
	bool ownsTransaction;
	bool completed = false;

	ScopedActiveClassInstantiation(
		ParseContext &parseContext, ClassDefinition *classDefinition, std::string requestKey, int symbolicIndex
	)
		: ownsTransaction(activeClassInstantiations.empty()) {
		if (ownsTransaction) {
			requireCompilerInvariant(
				!activeClassInstantiationTransaction.has_value(), "class instantiation transaction leaked between resolutions"
			);
			activeClassInstantiationTransaction = ClassInstantiationTransaction{&parseContext, {}, {}, false};
		} else {
			requireCompilerInvariant(
				activeClassInstantiationTransaction && activeClassInstantiationTransaction->parseContext == &parseContext,
				"nested class instantiation crossed parse contexts"
			);
		}
		activeClassInstantiations.push_back({&parseContext, classDefinition, std::move(requestKey), symbolicIndex});
	}

	~ScopedActiveClassInstantiation() {
		requireCompilerInvariant(!activeClassInstantiations.empty(), "active class instantiation stack underflow");
		activeClassInstantiations.pop_back();
		if (!completed)
			activeClassInstantiationTransaction->failed = true;
		if (!ownsTransaction)
			return;
		requireCompilerInvariant(activeClassInstantiations.empty(), "outer class instantiation did not unwind last");
		if (activeClassInstantiationTransaction->failed)
			rollbackClassInstantiationTransaction();
		activeClassInstantiationTransaction.reset();
		nextSymbolicClassInstantiationIndex = -2;
	}
};
