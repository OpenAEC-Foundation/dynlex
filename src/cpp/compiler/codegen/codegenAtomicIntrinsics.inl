if (isAtomicIntrinsicKind(kind)) {
	const size_t orderIndex = args.size() - 1;
	std::optional<AtomicMemoryOrder> sourceOrder = parseAtomicMemoryOrder(getCompileTimeString(context, args[orderIndex]));
	requireCompilerInvariant(sourceOrder.has_value(), "atomic operation reached codegen with an invalid memory order");
	auto toLLVMOrder = [](AtomicMemoryOrder order) {
		switch (order) {
		case AtomicMemoryOrder::Relaxed:
			return llvm::AtomicOrdering::Monotonic;
		case AtomicMemoryOrder::Acquire:
			return llvm::AtomicOrdering::Acquire;
		case AtomicMemoryOrder::Release:
			return llvm::AtomicOrdering::Release;
		case AtomicMemoryOrder::AcquireRelease:
			return llvm::AtomicOrdering::AcquireRelease;
		case AtomicMemoryOrder::SequentiallyConsistent:
			return llvm::AtomicOrdering::SequentiallyConsistent;
		}
		crashCompilerBug("unknown atomic memory order");
	};
	llvm::Value *pointer = nullptr;
	if (!generateRuntimeValue(args[1], pointer))
		return CodegenResult::failure();
	DataType pointerType = finalizedExpressionType(context, args[1]);
	DataType valueType = pointerType.dereferenced();
	bool booleanValue = valueType.kind == DataType::Kind::Bool && !valueType.isPointer();
	llvm::Type *storageType = booleanValue ? builder.getInt8Ty() : getLLVMType(context, valueType);
	llvm::Align alignment = booleanValue ? llvm::Align(1) : getLLVMABIAlignment(context, valueType);
	llvm::AtomicOrdering order = toLLVMOrder(*sourceOrder);
	auto toStorage = [&](llvm::Value *value) {
		return booleanValue ? builder.CreateZExt(value, storageType, "atomic_bool_store") : value;
	};
	auto fromStorage = [&](llvm::Value *value) -> llvm::Value * {
		return booleanValue ? builder.CreateTrunc(value, builder.getInt1Ty(), "atomic_bool_load") : value;
	};
	if (kind == IntrinsicKind::AtomicLoad) {
		llvm::LoadInst *load = builder.CreateAlignedLoad(storageType, pointer, alignment, "atomic_load");
		load->setAtomic(order);
		return fromStorage(load);
	}
	llvm::Value *value = nullptr;
	if (!generateRuntimeValue(args[2], value))
		return CodegenResult::failure();
	value = toStorage(value);
	if (kind == IntrinsicKind::AtomicStore) {
		llvm::StoreInst *store = builder.CreateAlignedStore(value, pointer, alignment);
		store->setAtomic(order);
		return nullptr;
	}
	llvm::AtomicRMWInst::BinOp operation = kind == IntrinsicKind::AtomicExchange   ? llvm::AtomicRMWInst::Xchg
										   : kind == IntrinsicKind::AtomicFetchAdd ? llvm::AtomicRMWInst::Add
																				   : llvm::AtomicRMWInst::Sub;
	llvm::AtomicRMWInst *result = builder.CreateAtomicRMW(operation, pointer, value, alignment, order);
	return fromStorage(result);
}
