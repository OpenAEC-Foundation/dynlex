[[gnu::noinline]] static CodegenResult
generateStaticReferenceIntrinsic(ParseContext &context, Expression *callExpr, const std::vector<Expression *> &args) {
	if (auto cached = context.staticLLVMValues.find(callExpr); cached != context.staticLLVMValues.end())
		return cached->second;
	DataType valueType = finalizedExpressionType(context, args[1]);
	auto *global = new llvm::GlobalVariable(
		*context.llvmModule, getLLVMType(context, valueType), true, llvm::GlobalValue::InternalLinkage, nullptr, "section.data"
	);
	global->setAlignment(getLLVMABIAlignment(context, valueType));
	context.staticLLVMValues[callExpr] = global;
	struct StaticReferenceScope {
		ParseContext &context;
		StaticReferenceScope(ParseContext &ctx) : context(ctx) { ++context.staticReferenceDepth; }
		~StaticReferenceScope() { --context.staticReferenceDepth; }
	} staticScope(context);
	CodegenResult value = generateExpressionCode(context, args[1]);
	if (!value)
		return value;
	auto *constant = llvm::dyn_cast<llvm::Constant>(value.value);
	if (!constant) {
		Diagnostic diagnostic;
		diagnostic.range = intrinsicDiagnosticRange(context, callExpr);
		diagnostic.message = "A static reference requires a constant aggregate value";
		context.addDiagnostic(std::move(diagnostic));
		return CodegenResult::failure();
	}
	global->setInitializer(constant);
	return global;
}

[[gnu::noinline]] static CodegenResult generateSectionDataIntrinsic(ParseContext &context, Expression *callExpr) {
	requireCompilerInvariant(callExpr->inferredDataValue, "section field reached codegen without its inferred aggregate");
	return generateExpressionCode(context, callExpr->inferredDataValue);
}

[[gnu::noinline]] static CodegenResult
generateConstructIntrinsic(ParseContext &context, const std::vector<Expression *> &args, const DataType &resultType) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	if (args.size() == 2) {
		requireCompilerInvariant(
			resultType.isConcrete() && resultType.isRuntimeValueType(),
			"value initialization reached codegen without a concrete runtime type"
		);
		return llvm::Constant::getNullValue(getLLVMType(context, resultType));
	}
	if (resultType.kind == DataType::Kind::Array) {
		llvm::Type *arrayType = getLLVMType(context, resultType);
		llvm::Value *array = llvm::Constant::getNullValue(arrayType);
		DataType elementType = *resultType.arrayElementType;
		for (size_t i = 2; i < args.size(); i++) {
			llvm::Value *elementVal = nullptr;
			if (!generateIntrinsicRuntimeValue(context, args[i], elementVal))
				return CodegenResult::failure();
			DataType fromType = finalizedExpressionType(context, args[i]);
			elementVal = ensureType(context, elementVal, fromType, elementType);
			if (typeHasManagedLifecycle(elementType) && !managedExpressionResultIsOwned(context, args[i]) &&
				!retainManagedValue(context, elementType, elementVal))
				return CodegenResult::failure();
			array = builder.CreateInsertValue(array, elementVal, {static_cast<unsigned>(i - 2)}, "array_element");
		}
		return array;
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
		if (!generateIntrinsicRuntimeValue(context, args[2], val))
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
	llvm::Value *result = llvm::Constant::getNullValue(getLLVMType(context, concreteType));
	std::vector<std::pair<DataType, llvm::Value *>> ownedManagedFields;

	for (size_t i = 0; i < inst.fieldTypes.size(); i++) {
		llvm::Value *fieldVal = nullptr;
		if (!generateIntrinsicRuntimeValue(context, args[i + 2], fieldVal))
			return CodegenResult::failure();
		DataType fieldFromType = finalizedExpressionType(context, args[i + 2]);
		fieldVal = ensureType(context, fieldVal, fieldFromType, inst.fieldTypes[i]);
		if (typeHasManagedLifecycle(inst.fieldTypes[i])) {
			if (!managedExpressionResultIsOwned(context, args[i + 2]) &&
				!retainManagedValue(context, inst.fieldTypes[i], fieldVal))
				return CodegenResult::failure();
			ownedManagedFields.push_back({inst.fieldTypes[i], fieldVal});
		}
		result = builder.CreateInsertValue(
			result, fieldVal, {getClassFieldLLVMIndex(context, concreteType, static_cast<int>(i))}, "class_field"
		);
	}

	if (classDef->retainSection) {
		if (!retainManagedValue(context, concreteType, result))
			return CodegenResult::failure();
		for (auto iterator = ownedManagedFields.rbegin(); iterator != ownedManagedFields.rend(); iterator++)
			if (!releaseManagedValue(context, iterator->first, iterator->second))
				return CodegenResult::failure();
	}
	return result;
}

[[gnu::noinline]] static CodegenResult
generatePropertyIntrinsic(ParseContext &context, Expression *callExpr, const std::vector<Expression *> &args) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	// Format: args[1]=instance, args[2]=fieldname (a compile-time property-name string)
	Expression *ownerExpr = args[1];
	DataType instType = finalizedExpressionType(context, ownerExpr).propertyOwnerType();
	ClassDefinition *classDef = instType.isPointer() ? nullptr : instType.classDefinition;

	std::string fieldName = getCompileTimeString(context, args[2]);

	// C strings expose a synthetic "data" property so string-library flexes can
	// operate on both heap strings and string literals without duplicating logic.
	if (fieldName == "data" && instType.isBytePointer())
		return generateExpressionCode(context, ownerExpr);

	if (!classDef) {
		context.diagnostics.push_back(Diagnostic(
			context, Diagnostic::Level::Error, "class has no properties", args[1]->range, "type", typeToUserName(instType)
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
			context, Diagnostic::Level::Error, "class missing property", args[1]->range, "type", typeToUserName(instType),
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
		llvm::Value *fieldValue = loadLLVMValue(context, fieldType, propertyAddress.address, fieldName + "_val");
		if (typeHasManagedLifecycle(fieldType) && !retainManagedValue(context, fieldType, fieldValue))
			return CodegenResult::failure();
		return fieldValue;
	}

	llvm::Value *instValue = nullptr;
	if (!generateIntrinsicRuntimeValue(context, ownerExpr, instValue))
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

static CodegenResult
generateShaderVertexInput(ParseContext &context, Expression *callExpr, const std::string &name, std::uint32_t location) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	DataType type = finalizedExpressionType(context, callExpr);
	context.registerShaderVertexInput(name, location, type, intrinsicDiagnosticRange(context, callExpr));
	std::string globalName = shaderVertexInputGlobalName(name, location, type);
	llvm::Type *valueType = getLLVMType(context, type);
	llvm::GlobalVariable *global = context.llvmModule->getGlobalVariable(globalName);
	if (!global) {
		constexpr unsigned spirvInputAddressSpace = 7;
		global = new llvm::GlobalVariable(
			*context.llvmModule, valueType, false, llvm::GlobalValue::ExternalLinkage, llvm::Constant::getNullValue(valueType),
			globalName, nullptr, llvm::GlobalValue::NotThreadLocal, spirvInputAddressSpace
		);
	}
	requireCompilerInvariant(global->getValueType() == valueType, "vertex input symbol has a conflicting LLVM type");
	llvm::LoadInst *input = builder.CreateLoad(valueType, global, name);
	input->setVolatile(true);
	return input;
}

static llvm::GlobalVariable *
generateShaderInterpolantGlobal(ParseContext &context, const std::vector<Expression *> &args, DataType type, bool output) {
	std::string name = getCompileTimeString(context, args[1]);
	auto location = getCompileTimeIntegerValue(resolveStoredCompileTimeValue(args[2], context.flexBindingFrames));
	requireCompilerInvariant(
		location && *location >= 0 && static_cast<std::uint64_t>(*location) <= UINT32_MAX,
		"shader interpolant reached codegen without a validated location"
	);
	if (!context.registerShaderInterpolant(
			{name, static_cast<std::uint32_t>(*location), type, getCompileTimeString(context, args[3]),
			 intrinsicDiagnosticRange(context, args[1])}
		))
		return nullptr;
	std::string globalName = shaderInterpolantGlobalName(name);
	llvm::Type *valueType = getLLVMType(context, type);
	auto *global = context.llvmModule->getGlobalVariable(globalName);
	if (!global) {
		global = new llvm::GlobalVariable(
			*context.llvmModule, valueType, false, llvm::GlobalValue::ExternalLinkage, llvm::Constant::getNullValue(valueType),
			globalName, nullptr, llvm::GlobalValue::NotThreadLocal, output ? 8 : 7
		);
	}
	requireCompilerInvariant(global->getValueType() == valueType, "shader interpolant global has a conflicting LLVM type");
	return global;
}

[[gnu::noinline]] static CodegenResult generateShaderInputIntrinsic(
	ParseContext &context, Expression *callExpr, const std::vector<Expression *> &args, IntrinsicKind kind
) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	std::string inputName = getCompileTimeString(context, args[1]);
	if (kind == IntrinsicKind::ShaderVertexInput) {
		auto location = getCompileTimeIntegerValue(resolveStoredCompileTimeValue(args[2], context.flexBindingFrames));
		requireCompilerInvariant(
			location && *location >= 0 && static_cast<std::uint64_t>(*location) <= UINT32_MAX,
			"vertex input reached codegen without its validated location"
		);
		return generateShaderVertexInput(context, callExpr, inputName, static_cast<std::uint32_t>(*location));
	}
	if (kind == IntrinsicKind::ShaderInput && inputName == "Position")
		return generateShaderVertexInput(context, callExpr, inputName, 0);
	DataType type = finalizedExpressionType(context, callExpr);
	llvm::Type *valueType = getLLVMType(context, type);
	llvm::GlobalVariable *global = nullptr;
	if (kind == IntrinsicKind::ShaderInterpolantInput) {
		global = generateShaderInterpolantGlobal(context, args, type, false);
		if (!global)
			return CodegenResult::failure();
	} else {
		requireCompilerInvariant(
			inputName == "FragCoord" || inputName == "FrontFacing", "unknown shader input reached codegen after type inference"
		);
		std::string globalName = "gl_" + inputName;
		global = context.llvmModule->getGlobalVariable(globalName);
		if (!global) {
			global = new llvm::GlobalVariable(
				*context.llvmModule, valueType, false, llvm::GlobalValue::ExternalLinkage,
				llvm::Constant::getNullValue(valueType), globalName, nullptr, llvm::GlobalValue::NotThreadLocal, 7
			);
		}
	}
	llvm::LoadInst *input = builder.CreateLoad(valueType, global, inputName);
	input->setVolatile(true);
	return input;
}

[[gnu::noinline]] static CodegenResult
generateShaderUniformIntrinsic(ParseContext &context, Expression *callExpr, const std::vector<Expression *> &args) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	// The SPIR-V patcher wraps the value in a set-zero uniform block.
	std::string uniformName = getCompileTimeString(context, args[1]);
	requireCompilerInvariant(!uniformName.empty(), "shader uniform reached codegen without a validated name");
	std::optional<std::int64_t> bindingValue =
		getCompileTimeIntegerValue(resolveStoredCompileTimeValue(args[2], context.flexBindingFrames));
	requireCompilerInvariant(
		bindingValue && *bindingValue >= 0 && static_cast<std::uint64_t>(*bindingValue) <= UINT32_MAX,
		"shader uniform reached codegen without a validated binding"
	);
	DataType valueType = finalizedExpressionType(context, callExpr);
	if (!context.registerShaderUniform(
			uniformName, static_cast<std::uint32_t>(*bindingValue), valueType, intrinsicDiagnosticRange(context, callExpr)
		))
		return CodegenResult::failure();
	llvm::Type *llvmType = getLLVMType(context, valueType);
	llvm::Type *storageType = llvmType;
	const bool matrix = valueType.kind == DataType::Kind::Matrix;
	if (matrix) {
		llvm::SmallVector<llvm::Type *, 8> fields;
		auto *rowType = llvm::FixedVectorType::get(builder.getFloatTy(), valueType.matrixColumns());
		for (int row = 0; row < valueType.matrixRows(); ++row) {
			fields.push_back(rowType);
			if (valueType.matrixColumns() == 2)
				fields.push_back(llvm::FixedVectorType::get(builder.getFloatTy(), 2));
			else if (valueType.matrixColumns() == 3)
				fields.push_back(builder.getFloatTy());
		}
		storageType = llvm::StructType::get(builder.getContext(), fields);
	}
	std::string globalName = "ubo_" + uniformName;
	llvm::GlobalVariable *global = context.llvmModule->getGlobalVariable(globalName);
	if (!global) {
		global = new llvm::GlobalVariable(
			*context.llvmModule, storageType, false, llvm::GlobalValue::ExternalLinkage, nullptr, globalName, nullptr,
			llvm::GlobalValue::NotThreadLocal, 3
		);
		global->setInitializer(llvm::Constant::getNullValue(storageType));
	}
	requireCompilerInvariant(global->getValueType() == storageType, "uniform global disagrees with its registered type");
	llvm::Value *stored = builder.CreateLoad(storageType, global, uniformName + "_val");
	if (!matrix)
		return stored;
	// Explicit padding remains visible in WGSL, which has no array-stride annotation.
	llvm::Value *value = llvm::Constant::getNullValue(llvmType);
	const unsigned step = valueType.matrixColumns() < 4 ? 2 : 1;
	for (unsigned row = 0; row < static_cast<unsigned>(valueType.matrixRows()); ++row) {
		llvm::Value *field = builder.CreateExtractValue(stored, row * step, "uniform_row");
		value = builder.CreateInsertValue(value, field, row, "uniform_matrix");
	}
	return value;
}

[[gnu::noinline]] static CodegenResult
generateShaderOutputIntrinsic(ParseContext &context, const std::vector<Expression *> &args, IntrinsicKind kind) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	const bool interpolant = kind == IntrinsicKind::ShaderInterpolantOutput;
	Expression *operand = args[interpolant ? 4 : 1];
	llvm::Value *value = nullptr;
	if (!generateIntrinsicRuntimeValue(context, operand, value))
		return CodegenResult::failure();
	llvm::GlobalVariable *global = nullptr;
	if (interpolant) {
		global = generateShaderInterpolantGlobal(context, args, finalizedExpressionType(context, operand), true);
		if (!global)
			return CodegenResult::failure();
	} else {
		DataType vector{DataType::Kind::Vector};
		vector.arraySize = 4;
		vector.arrayElementType = std::make_shared<DataType>(DataType::Kind::Float, 4);
		value = ensureType(context, value, finalizedExpressionType(context, operand), vector);
		std::string name = context.options.shaderStage == ParseContext::ShaderStage::Vertex ? "gl_Position" : "gl_FragColor";
		global = context.llvmModule->getGlobalVariable(name);
		requireCompilerInvariant(global != nullptr, "validated shader output is missing its codegen global");
	}
	llvm::StoreInst *store = builder.CreateStore(value, global);
	store->setMetadata(shaderOutputMetadataName, llvm::MDNode::get(*context.llvmContext, {}));
	return nullptr;
}

[[gnu::noinline]] static CodegenResult
generateExtractElementIntrinsic(ParseContext &context, const std::vector<Expression *> &args) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	DataType aggregateType = finalizedExpressionType(context, args[1]);
	if (aggregateType.kind == DataType::Kind::ArgumentPack)
		return nullptr; // Selecting a captured source expression produces no runtime value.
	llvm::Value *aggregate = nullptr;
	if (!generateIntrinsicRuntimeValue(context, args[1], aggregate))
		return CodegenResult::failure();
	int constantIndex = 0;
	if (resolveStoredCompileTimeInteger(args[2], context.flexBindingFrames, constantIndex)) {
		requireCompilerInvariant(
			constantIndex >= 0 && constantIndex < aggregateType.arraySize,
			"validated aggregate index is outside its fixed extent"
		);
		if (aggregateType.kind == DataType::Kind::Array)
			return builder.CreateExtractValue(aggregate, static_cast<unsigned>(constantIndex), "elem");
		return builder.CreateExtractElement(
			aggregate, getVectorLaneIndexValue(context, static_cast<unsigned>(constantIndex)), "elem"
		);
	}
	llvm::Value *idx = nullptr;
	if (!generateIntrinsicRuntimeValue(context, args[2], idx))
		return CodegenResult::failure();
	idx = ensureType(context, idx, finalizedExpressionType(context, args[2]), {DataType::Kind::Int, 4});
	if (aggregateType.kind == DataType::Kind::Vector)
		return builder.CreateExtractElement(aggregate, idx, "elem");
	requireCompilerInvariant(
		aggregateType.kind == DataType::Kind::Array && aggregateType.arrayElementType,
		"validated element extraction reached codegen with a non-aggregate type"
	);
	llvm::AllocaInst *storage = createEntryAlloca(context, "aggregate_extract", aggregateType);
	builder.CreateStore(aggregate, storage);
	llvm::Value *address =
		builder.CreateInBoundsGEP(getLLVMType(context, aggregateType), storage, {builder.getInt32(0), idx}, "element_address");
	return loadLLVMValue(context, *aggregateType.arrayElementType, address, "elem");
}

[[gnu::noinline]] static CodegenResult
generateInsertElementIntrinsic(ParseContext &context, const std::vector<Expression *> &args) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	llvm::Value *aggregate = nullptr;
	llvm::Value *element = nullptr;
	if (!generateIntrinsicRuntimeValue(context, args[1], aggregate) ||
		!generateIntrinsicRuntimeValue(context, args[3], element))
		return CodegenResult::failure();
	DataType aggregateType = finalizedExpressionType(context, args[1]);
	DataType elementType = *aggregateType.arrayElementType;
	element = ensureType(context, element, finalizedExpressionType(context, args[3]), elementType);
	int constantIndex = 0;
	if (resolveStoredCompileTimeInteger(args[2], context.flexBindingFrames, constantIndex)) {
		requireCompilerInvariant(
			constantIndex >= 0 && constantIndex < aggregateType.arraySize,
			"validated aggregate index is outside its fixed extent"
		);
		if (aggregateType.kind == DataType::Kind::Array)
			return builder.CreateInsertValue(aggregate, element, static_cast<unsigned>(constantIndex), "aggregate_insert");
		return builder.CreateInsertElement(
			aggregate, element, getVectorLaneIndexValue(context, static_cast<unsigned>(constantIndex)), "vector_insert"
		);
	}
	llvm::Value *index = nullptr;
	if (!generateIntrinsicRuntimeValue(context, args[2], index))
		return CodegenResult::failure();
	index = ensureType(context, index, finalizedExpressionType(context, args[2]), {DataType::Kind::Int, 4});
	if (aggregateType.kind == DataType::Kind::Vector)
		return builder.CreateInsertElement(aggregate, element, index, "vector_insert");
	llvm::AllocaInst *storage = createEntryAlloca(context, "aggregate_insert", aggregateType);
	builder.CreateStore(aggregate, storage);
	llvm::Value *address = builder.CreateInBoundsGEP(
		getLLVMType(context, aggregateType), storage, {builder.getInt32(0), index}, "element_address"
	);
	builder.CreateAlignedStore(element, address, getLLVMABIAlignment(context, elementType));
	return loadLLVMValue(context, aggregateType, storage, "aggregate_inserted");
}
