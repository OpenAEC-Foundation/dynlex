#include "classDefinition.h"
#include "codegenInternal.h"
#include "compilerUtils.h"
#include "intrinsicInfo.h"
#include "patternDefinition.h"
#include "section.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include <algorithm>

namespace {

Instantiation &lifecycleInstantiation(const DataType &type, Section *section) {
	InstantiationKey key{.argumentTypes = {type}, .compileTimeParameters = {}};
	auto instantiation = section->instantiations.find(key);
	requireCompilerInvariant(
		instantiation != section->instantiations.end(), "managed lifecycle reached codegen without inferred behavior"
	);
	requireCompilerInvariant(instantiation->second.valid, "invalid managed lifecycle reached codegen");
	requireCompilerInvariant(
		instantiation->second.returnType.kind == DataType::Kind::Void,
		"managed lifecycle reached codegen with a non-void return type"
	);
	return instantiation->second;
}

bool invokeCustomLifecycle(ParseContext &context, const DataType &type, llvm::Value *value, Section *section) {
	requireCompilerInvariant(value != nullptr, "managed lifecycle received a null LLVM value");
	Instantiation &instantiation = lifecycleInstantiation(type, section);
	Expression placeholder;
	placeholder.kind = Expression::Kind::TypedPlaceholder;
	placeholder.type = type;
	std::vector<std::pair<std::string, Expression *>> bindings = {{std::string(managedLifecycleParameterName), &placeholder}};
	if (!instantiation.llvmFunction && !generateSpecializedFunction(context, section, bindings, instantiation))
		return false;
	requireCompilerInvariant(instantiation.llvmFunction != nullptr, "managed lifecycle function was not generated");
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	llvm::AllocaInst *valueAddress = createEntryAlloca(context, "managed_value", type);
	builder.CreateAlignedStore(value, valueAddress, getLLVMABIAlignment(context, type));
	builder.CreateCall(instantiation.llvmFunction, {valueAddress});
	return true;
}

bool applyManagedLifecycle(ParseContext &context, const DataType &type, llvm::Value *value, bool retain) {
	if (type.kind == DataType::Kind::Array) {
		if (!type.arrayElementType || !typeHasManagedLifecycle(*type.arrayElementType))
			return true;
		requireCompilerInvariant(type.arraySize >= 0, "managed array lifecycle requires a fixed element count");
		auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
		if (retain) {
			for (int index = 0; index < type.arraySize; index++) {
				llvm::Value *element = builder.CreateExtractValue(value, {static_cast<unsigned>(index)}, "managed_element");
				if (!applyManagedLifecycle(context, *type.arrayElementType, element, true))
					return false;
			}
		} else {
			for (int index = type.arraySize; index-- > 0;) {
				llvm::Value *element = builder.CreateExtractValue(value, {static_cast<unsigned>(index)}, "managed_element");
				if (!applyManagedLifecycle(context, *type.arrayElementType, element, false))
					return false;
			}
		}
		return true;
	}
	if (type.kind != DataType::Kind::Class || type.isPointer())
		return true;
	requireCompilerInvariant(
		type.classDefinition && type.classInstIndex >= 0, "managed lifecycle requires a concrete class type"
	);
	ClassDefinition &definition = *type.classDefinition;
	Section *customSection = retain ? definition.retainSection : definition.releaseSection;
	if (customSection)
		return invokeCustomLifecycle(context, type, value, customSection);
	const auto &fields = definition.instantiations[type.classInstIndex].fieldTypes;
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	if (retain) {
		for (size_t index = 0; index < fields.size(); index++) {
			if (!typeHasManagedLifecycle(fields[index]))
				continue;
			llvm::Value *field = builder.CreateExtractValue(
				value, {getClassFieldLLVMIndex(context, type, static_cast<int>(index))}, "managed_field"
			);
			if (!applyManagedLifecycle(context, fields[index], field, true))
				return false;
		}
	} else {
		for (size_t index = fields.size(); index-- > 0;) {
			if (!typeHasManagedLifecycle(fields[index]))
				continue;
			llvm::Value *field = builder.CreateExtractValue(
				value, {getClassFieldLLVMIndex(context, type, static_cast<int>(index))}, "managed_field"
			);
			if (!applyManagedLifecycle(context, fields[index], field, false))
				return false;
		}
	}
	return true;
}

ParseContext::ManagedStorageState *findManagedStorage(ParseContext &context, llvm::Value *address) {
	for (ParseContext::ManagedStorageState &storage : context.managedLocalStorage) {
		if (storage.address == address)
			return &storage;
	}
	for (ParseContext::ManagedStorageState &storage : context.managedGlobalStorage) {
		if (storage.address == address)
			return &storage;
	}
	return nullptr;
}

bool releaseInitializedStorage(ParseContext &context, ParseContext::ManagedStorageState &storage) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	requireCompilerInvariant(builder.GetInsertBlock() != nullptr, "managed cleanup requires an insertion block");
	if (builder.GetInsertBlock()->hasTerminator())
		return true;
	llvm::Function *function = builder.GetInsertBlock()->getParent();
	requireCompilerInvariant(function != nullptr, "managed cleanup requires an active function");
	llvm::Value *initialized = builder.CreateLoad(builder.getInt1Ty(), storage.initializedAddress, "managed_initialized");
	llvm::BasicBlock *releaseBlock = llvm::BasicBlock::Create(*context.llvmContext, "managed.release", function);
	llvm::BasicBlock *continueBlock = llvm::BasicBlock::Create(*context.llvmContext, "managed.continue", function);
	builder.CreateCondBr(initialized, releaseBlock, continueBlock);
	builder.SetInsertPoint(releaseBlock);
	llvm::Value *value = builder.CreateAlignedLoad(
		getLLVMType(context, storage.type), storage.address, getLLVMABIAlignment(context, storage.type), "managed_stored_value"
	);
	if (!releaseManagedValue(context, storage.type, value))
		return false;
	builder.CreateStore(builder.getFalse(), storage.initializedAddress);
	builder.CreateBr(continueBlock);
	builder.SetInsertPoint(continueBlock);
	return true;
}

} // namespace

bool managedExpressionResultIsOwned(ParseContext &context, Expression *expression) {
	if (!expression || !typeHasManagedLifecycle(finalizedExpressionType(context, expression)))
		return false;
	ResolvedBindingLayers resolved = resolveCodegenBindingLayers(context, expression, context.flexBindingFrames);
	if (resolved.expression && resolved.expression != expression) {
		FlexBindingScope scope(context, std::move(resolved.bindingFrameStack));
		return managedExpressionResultIsOwned(context, resolved.expression);
	}
	if (expression->kind == Expression::Kind::IntrinsicCall) {
		IntrinsicKind kind = intrinsicKind(expression->intrinsicName);
		return kind != IntrinsicKind::LifecycleValue && kind != IntrinsicKind::Dereference;
	}
	return true;
}

bool retainManagedValue(ParseContext &context, const DataType &type, llvm::Value *value) {
	requireCompilerInvariant(typeHasManagedLifecycle(type), "retain requested for an unmanaged type");
	return applyManagedLifecycle(context, type, value, true);
}

bool releaseManagedValue(ParseContext &context, const DataType &type, llvm::Value *value) {
	requireCompilerInvariant(typeHasManagedLifecycle(type), "release requested for an unmanaged type");
	return applyManagedLifecycle(context, type, value, false);
}

static void
registerManagedStorage(ParseContext &context, llvm::Value *address, const DataType &type, Section *ownerSection, bool global) {
	requireCompilerInvariant(address != nullptr, "managed storage registration requires an address");
	requireCompilerInvariant(typeHasManagedLifecycle(type), "unmanaged storage was registered for cleanup");
	requireCompilerInvariant(findManagedStorage(context, address) == nullptr, "managed storage was registered twice");
	requireCompilerInvariant(global != static_cast<bool>(ownerSection), "managed storage ownership is inconsistent");
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	llvm::Value *initializedAddress = nullptr;
	if (global) {
		initializedAddress = new llvm::GlobalVariable(
			*context.llvmModule, builder.getInt1Ty(), false, llvm::GlobalValue::InternalLinkage, builder.getFalse(),
			"managed_global_initialized"
		);
	} else {
		llvm::Function *function = builder.GetInsertBlock() ? builder.GetInsertBlock()->getParent() : nullptr;
		requireCompilerInvariant(function != nullptr, "managed local storage requires an active function");
		llvm::IRBuilder<> entryBuilder(&function->getEntryBlock(), function->getEntryBlock().begin());
		llvm::AllocaInst *initialized = entryBuilder.CreateAlloca(entryBuilder.getInt1Ty(), nullptr, "managed_initialized");
		entryBuilder.CreateStore(entryBuilder.getFalse(), initialized);
		initializedAddress = initialized;
	}
	ParseContext::ManagedStorageState storage{address, initializedAddress, type, ownerSection};
	(global ? context.managedGlobalStorage : context.managedLocalStorage).push_back(std::move(storage));
}

void registerManagedStorage(ParseContext &context, llvm::Value *address, const DataType &type, Section *ownerSection) {
	registerManagedStorage(context, address, type, ownerSection, false);
}

void registerManagedGlobalStorage(ParseContext &context, llvm::Value *address, const DataType &type) {
	registerManagedStorage(context, address, type, nullptr, true);
}

void initializeManagedStorage(ParseContext &context, llvm::Value *address, const DataType &type, llvm::Value *ownedValue) {
	requireCompilerInvariant(address != nullptr, "managed initialization requires a destination address");
	requireCompilerInvariant(ownedValue != nullptr, "managed initialization requires an owned value");
	requireCompilerInvariant(typeHasManagedLifecycle(type), "managed initialization received an unmanaged type");
	ParseContext::ManagedStorageState *storage = findManagedStorage(context, address);
	requireCompilerInvariant(storage != nullptr, "managed initialization requires registered storage");
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	builder.CreateAlignedStore(ownedValue, address, getLLVMABIAlignment(context, type));
	builder.CreateStore(builder.getTrue(), storage->initializedAddress);
}

bool storeManagedValue(ParseContext &context, llvm::Value *address, const DataType &type, llvm::Value *ownedValue) {
	requireCompilerInvariant(address != nullptr, "managed store requires a destination address");
	requireCompilerInvariant(ownedValue != nullptr, "managed store requires an owned value");
	requireCompilerInvariant(typeHasManagedLifecycle(type), "managed store received an unmanaged type");
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	ParseContext::ManagedStorageState *storage = findManagedStorage(context, address);
	if (storage) {
		if (!releaseInitializedStorage(context, *storage))
			return false;
	} else {
		llvm::Value *oldValue =
			builder.CreateAlignedLoad(getLLVMType(context, type), address, getLLVMABIAlignment(context, type), "managed_old");
		if (!releaseManagedValue(context, type, oldValue))
			return false;
	}
	builder.CreateAlignedStore(ownedValue, address, getLLVMABIAlignment(context, type));
	if (storage)
		builder.CreateStore(builder.getTrue(), storage->initializedAddress);
	return true;
}

bool releaseManagedTemporaryStorage(ParseContext &context, llvm::Value *address) {
	auto storage = std::find_if(
		context.managedLocalStorage.begin(), context.managedLocalStorage.end(),
		[address](const ParseContext::ManagedStorageState &candidate) {
		return candidate.address == address;
	}
	);
	requireCompilerInvariant(storage != context.managedLocalStorage.end(), "managed temporary storage is not registered");
	if (!releaseInitializedStorage(context, *storage))
		return false;
	context.managedLocalStorage.erase(storage);
	return true;
}

bool releaseManagedStorageForSection(ParseContext &context, Section *ownerSection) {
	requireCompilerInvariant(ownerSection != nullptr, "managed storage cleanup requires an owning source section");
	for (size_t index = context.managedLocalStorage.size(); index-- > 0;) {
		if (context.managedLocalStorage[index].ownerSection == ownerSection &&
			!releaseInitializedStorage(context, context.managedLocalStorage[index]))
			return false;
	}
	std::erase_if(context.managedLocalStorage, [ownerSection](const ParseContext::ManagedStorageState &storage) {
		return storage.ownerSection == ownerSection;
	});
	return true;
}

bool releaseManagedStorageForReturn(ParseContext &context) {
	for (size_t index = context.managedLocalStorage.size(); index-- > 0;)
		if (!releaseInitializedStorage(context, context.managedLocalStorage[index]))
			return false;
	return true;
}

bool releaseAllManagedStorage(ParseContext &context) {
	for (size_t index = context.managedLocalStorage.size(); index-- > 0;)
		if (!releaseInitializedStorage(context, context.managedLocalStorage[index]))
			return false;
	for (size_t index = context.managedGlobalStorage.size(); index-- > 0;)
		if (!releaseInitializedStorage(context, context.managedGlobalStorage[index]))
			return false;
	return true;
}
