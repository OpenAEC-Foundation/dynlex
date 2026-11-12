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

static ClassInstantiationRequest buildClassInstantiationRequest(
	const BindingFrameStack &bindingFrameStack, InferenceContext *inferenceContext,
	const std::vector<DataType> *constructionArgumentTypes
) {
	ClassInstantiationRequest request;
	auto &parameters = request.parameters;
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
	if (constructionArgumentTypes)
		request.constructionArgumentTypes = *constructionArgumentTypes;
	return request;
}

std::string classInstantiationRequestKey(const ClassInstantiationRequest &request) {
	std::string key;
	for (const auto &[name, type, value] : request.parameters) {
		key += std::to_string(name.size()) + ":" + name;
		key += "=" + encodeDataTypeForCacheKey(type);
		key += ":" + encodeCompileTimeValueForCacheKey(value) + ";";
	}
	if (request.constructionArgumentTypes) {
		key += "#construct";
		for (const DataType &type : *request.constructionArgumentTypes)
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

static void cacheClassInstantiationRequest(
	ClassDefinition *classDefinition, const std::string &requestKey, ClassInstantiationRequest request, int index,
	InferenceContext *context
) {
	request.index = index;
	auto [it, inserted] = classDefinition->instantiationRequests.emplace(
		requestKey, std::make_shared<const ClassInstantiationRequest>(std::move(request))
	);
	if (!inserted) {
		requireCompilerInvariant(it->second->index == index, "class instantiation request resolved to inconsistent indices");
		return;
	}
	requireCompilerInvariant(
		activeClassInstantiationTransaction.has_value(), "class instantiation request cache has no active transaction"
	);
	activeClassInstantiationTransaction->insertedRequestKeys.push_back({classDefinition, requestKey});
	recordInferenceClassRequestRestoration(context, classDefinition, requestKey);
}

static void rollbackClassInstantiationTransaction() {
	requireCompilerInvariant(
		activeClassInstantiationTransaction.has_value(), "cannot roll back a missing class instantiation transaction"
	);
	for (auto it = activeClassInstantiationTransaction->insertedRequestKeys.rbegin();
		 it != activeClassInstantiationTransaction->insertedRequestKeys.rend(); ++it) {
		it->first->instantiationRequests.erase(it->second);
	}
	for (const auto &[classDefinition, originalSize] : activeClassInstantiationTransaction->originalInstantiationSizes) {
		requireCompilerInvariant(
			classDefinition->instantiations.size() >= originalSize,
			"class instantiation transaction observed an unexpected instantiation-list shrink"
		);
		classDefinition->instantiations.resize(originalSize);
	}
}

// A discarded signature probe must not poison or commit into an enclosing
// class-layout transaction. Keep a savepoint for the nested speculative work.
struct ScopedClassInstantiationSavepoint {
	std::optional<ClassInstantiationTransaction> saved;
	std::unordered_map<ClassDefinition *, size_t> savedSizes;

	ScopedClassInstantiationSavepoint() : saved(activeClassInstantiationTransaction) {
		if (!saved)
			return;
		for (const auto &[definition, originalSize] : saved->originalInstantiationSizes)
			savedSizes.emplace(definition, definition->instantiations.size());
	}

	~ScopedClassInstantiationSavepoint() {
		if (!saved)
			return;
		requireCompilerInvariant(
			activeClassInstantiationTransaction.has_value(), "nested class probe lost its outer transaction"
		);
		auto &current = *activeClassInstantiationTransaction;
		for (size_t index = current.insertedRequestKeys.size(); index > saved->insertedRequestKeys.size(); --index) {
			const auto &[definition, request] = current.insertedRequestKeys[index - 1];
			definition->instantiationRequests.erase(request);
		}
		for (const auto &[definition, originalSize] : current.originalInstantiationSizes) {
			auto previous = savedSizes.find(definition);
			size_t size = previous == savedSizes.end() ? originalSize : previous->second;
			requireCompilerInvariant(definition->instantiations.size() >= size, "nested class probe shrank an outer layout");
			definition->instantiations.resize(size);
		}
		activeClassInstantiationTransaction = std::move(saved);
	}
};

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
	if (type.functionSignature) {
		auto signature = std::make_shared<FunctionSignature>(*type.functionSignature);
		bool signatureChanged =
			replaceSymbolicClassInstantiation(signature->result, classDefinition, symbolicIndex, concreteIndex);
		for (DataType &parameter : signature->parameters)
			signatureChanged =
				replaceSymbolicClassInstantiation(parameter, classDefinition, symbolicIndex, concreteIndex) || signatureChanged;
		if (signatureChanged) {
			type.functionSignature = std::move(signature);
			changed = true;
		}
	}
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
