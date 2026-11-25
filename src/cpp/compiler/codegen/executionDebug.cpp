#include "executionDebug.h"
#include "classDefinition.h"
#include "codegenInternal.h"
#include "expression.h"
#include "pathUtils.h"
#include "sourceFile.h"
#include "textEncoding.h"
#include "variable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include <map>
#include <nlohmann/json.hpp>

namespace {
using Json = nlohmann::json;

Json location(const Range &range) {
	const auto start = range.sourceStart(), end = range.sourceEnd();
	requireCompilerInvariant(start.sourceFile && end.sourceFile, "debug point has no authored source range");
	return {
		{"uri", pathutil::toAbsoluteUri(start.sourceFile->uri)},
		{"range",
		 {{"start",
		   {{"line", start.sourceFileLineIndex},
			{"character", lsp::utf16Column(start.sourceFile->getLine(start.sourceFileLineIndex), start.column)}}},
		  {"end",
		   {{"line", end.sourceFileLineIndex},
			{"character", lsp::utf16Column(end.sourceFile->getLine(end.sourceFileLineIndex), end.column)}}}}}
	};
}

struct Types {
	ParseContext &context;
	std::map<DataType, size_t> indices;
	Json entries = Json::array();

	size_t add(DataType type) {
		if (auto found = indices.find(type); found != indices.end())
			return found->second;
		const size_t index = entries.size();
		indices.emplace(type, index);
		entries.push_back(nullptr);
		auto &layout = context.llvmModule->getDataLayout();
		auto *llvmType = getLLVMType(context, type);
		Json entry = {{"name", type.toString()}, {"size", layout.getTypeAllocSize(llvmType).getFixedValue()}};
		if (type.isPointer()) {
			entry["kind"] = "pointer";
			--type.pointerDepth;
			if (type.isRuntimeValueType())
				entry["element"] = add(type);
		} else if (type.kind == DataType::Kind::Class) {
			entry["kind"] = "struct";
			entry["fields"] = Json::array();
			const auto &instantiation = type.classDefinition->instantiations.at(type.classInstIndex);
			const auto *structure = layout.getStructLayout(instantiation.llvmStructType);
			for (size_t field = 0; field < instantiation.fieldTypes.size(); ++field)
				entry["fields"].push_back(
					{{"name", type.classDefinition->fields[field].name},
					 {"offset", structure->getElementOffset(instantiation.llvmFieldIndices.at(field)).getFixedValue()},
					 {"type", add(instantiation.fieldTypes[field])}}
				);
		} else if (type.hasAggregateElementType()) {
			entry["kind"] = "array";
			DataType element = type.aggregateElementType();
			if (type.kind == DataType::Kind::Matrix) {
				element = DataType(DataType::Kind::Vector);
				element.arraySize = type.matrixRows();
				element.arrayElementType = type.arrayElementType;
			}
			entry["count"] = type.kind == DataType::Kind::Matrix ? type.matrixColumns() : type.arraySize;
			entry["stride"] = layout.getTypeAllocSize(getLLVMType(context, element)).getFixedValue();
			entry["element"] = add(element);
		} else if (type.kind == DataType::Kind::Bool) {
			entry["kind"] = "boolean";
		} else if (type.kind == DataType::Kind::Float) {
			entry["kind"] = "float";
		} else if (type.isInteger()) {
			entry["kind"] = "integer";
			entry["signed"] = type.isSignedInteger();
			entry["bits"] = type.numericSize * 8;
		} else {
			requireCompilerInvariant(type.isFunction(), "debug variable has no runtime representation");
			entry["kind"] = "function";
		}
		entries[index] = std::move(entry);
		return index;
	}
};
} // namespace

void emitDebugLocation(ParseContext &context, const Range &range) {
	if (!context.options.debugExecution)
		return;
	const auto start = range.sourceStart();
	requireCompilerInvariant(start.sourceFile, "debug point has no source file");
	if (start.sourceFile->isInternal)
		return;
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	auto *function = builder.GetInsertBlock()->getParent();
	Json metadata = location(range);
	metadata["variables"] = Json::array();
	Types types{context, {}};
	std::vector<llvm::Value *> addresses;
	struct DebugVariable {
		VariableStorageKey key;
		llvm::Value *address;
		DataType type;
		bool initialized;
	};
	std::map<std::string, DebugVariable> visible;
	std::set<std::string> shadowed;
	for (Section *section = range.section(); section; section = section->parent) {
		for (const auto &[name, definition] : section->variableDefinitions) {
			if (!shadowed.insert(name).second)
				continue;
			auto key = variableStorageKey(definition, context.currentInstantiatedSectionBody);
			bool initialized = key.definition->range.line->mergedLineIndex < range.line->mergedLineIndex;
			Variable *variable = section->findVariable(definition);
			requireCompilerInvariant(variable, "debug declaration has no variable");
			if (variable->dataInitializer) {
				Expression *initializer = variable->dataInitializer;
				llvm::Value *address = nullptr;
				if (variable->isGlobal) {
					if (auto storage = context.globalLLVMVariables.find(name); storage != context.globalLLVMVariables.end())
						address = storage->second;
				} else {
					if (key.scope) {
						Expression *clone = nullptr;
						for (auto *body = context.currentInstantiatedSectionBody; body && !clone; body = body->parentBody)
							clone = body->findCloneOf(initializer);
						requireCompilerInvariant(clone, "debug local data has no instantiated declaration");
						initializer = clone;
					}
					if (auto stored = context.localDataLLVMValues.find(initializer);
						stored != context.localDataLLVMValues.end()) {
						address = stored->second;
						if (auto *constant = llvm::dyn_cast_or_null<llvm::Constant>(address))
							address = new llvm::GlobalVariable(
								*context.llvmModule, constant->getType(), true, llvm::GlobalValue::PrivateLinkage, constant,
								"debug.constant"
							);
					}
				}
				if (address)
					visible.emplace(
						name, DebugVariable{key, address, finalizedExpressionType(context, initializer), initialized}
					);
				continue;
			}
			if (auto parameter = context.codegenParameterBindings.find(key.definition);
				parameter != context.codegenParameterBindings.end())
				visible.emplace(name, DebugVariable{key, parameter->second, context.finalizedVariableTypes.at(key), true});
			else if (auto storage = context.variableStorage.find(key); storage != context.variableStorage.end()) {
				if (auto *local = llvm::dyn_cast<llvm::AllocaInst>(storage->second); local && local->getFunction() != function)
					continue;
				visible.emplace(name, DebugVariable{key, storage->second, context.finalizedVariableTypes.at(key), initialized});
			}
		}
	}
	for (const auto &[name, storage] : visible) {
		const auto &[key, address, type, initialized] = storage;
		Json variable = location(key.definition->range);
		variable["name"] = name;
		variable["type"] = types.add(type);
		variable["initialized"] = initialized;
		metadata["variables"].push_back(std::move(variable));
		addresses.push_back(address);
	}
	metadata["types"] = std::move(types.entries);
	auto *pointer = builder.getPtrTy();
	auto *slotsType = llvm::ArrayType::get(pointer, addresses.size());
	auto &entry = function->getEntryBlock();
	llvm::IRBuilder<> allocator(&entry, entry.begin());
	auto *slots = allocator.CreateAlloca(slotsType, nullptr, "debug.variables");
	for (size_t index = 0; index < addresses.size(); ++index)
		builder.CreateStore(
			addresses[index], builder.CreateInBoundsGEP(slotsType, slots, {builder.getInt32(0), builder.getInt32(index)})
		);
	auto callback = context.llvmModule->getOrInsertFunction(
		"dynlex_debug_location", llvm::FunctionType::get(builder.getVoidTy(), {pointer, pointer}, false)
	);
	builder.CreateCall(callback, {builder.CreateGlobalString(metadata.dump()), slots});
	function->setMetadata("dynlex.debug.frame", llvm::MDNode::get(*context.llvmContext, {}));
}

void finalizeDebugInstrumentation(ParseContext &context) {
	if (!context.options.debugExecution)
		return;
	for (auto &function : *context.llvmModule) {
		if (!function.getMetadata("dynlex.debug.frame"))
			continue;
		auto &entry = function.getEntryBlock();
		llvm::IRBuilder<> builder(&entry, entry.begin());
		llvm::Value *initialLocation = nullptr;
		for (auto &block : function)
			for (auto &instruction : block)
				if (auto *call = llvm::dyn_cast<llvm::CallInst>(&instruction);
					call && call->getCalledFunction() && call->getCalledFunction()->getName() == "dynlex_debug_location") {
					initialLocation = call->getArgOperand(0);
					break;
				}
		requireCompilerInvariant(initialLocation, "debug frame has no source point");
		std::string name = function.hasFnAttribute("dynlex.debug.name")
							   ? function.getFnAttribute("dynlex.debug.name").getValueAsString().str()
							   : function.getName().str();
		auto enter = context.llvmModule->getOrInsertFunction(
			"dynlex_debug_enter", llvm::FunctionType::get(builder.getVoidTy(), {builder.getPtrTy(), builder.getPtrTy()}, false)
		);
		auto leave = context.llvmModule->getOrInsertFunction(
			"dynlex_debug_leave", llvm::FunctionType::get(builder.getVoidTy(), {}, false)
		);
		builder.CreateCall(enter, {builder.CreateGlobalString(name), initialLocation});
		for (auto &block : function) {
			if (auto *returned = llvm::dyn_cast<llvm::ReturnInst>(block.getTerminator())) {
				builder.SetInsertPoint(returned);
				builder.CreateCall(leave);
			}
		}
	}
}
