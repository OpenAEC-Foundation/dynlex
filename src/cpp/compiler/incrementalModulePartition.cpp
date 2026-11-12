#include "incrementalModulePartition.h"
#include "compilerUtils.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalIFunc.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Module.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/ValueMapper.h"
#include <queue>
#include <unordered_set>

namespace {
class PartitionMaterializer : public llvm::ValueMaterializer {
	llvm::Module &destination;
	llvm::ValueToValueMapTy values;
	std::unordered_set<const llvm::GlobalValue *> definitions;
	std::queue<const llvm::GlobalValue *> pendingInterfaces;
	ModulePartitionCloneWork &work;

	void copyComdat(llvm::GlobalObject &copy, const llvm::GlobalObject &original) {
		if (const llvm::Comdat *group = original.getComdat()) {
			auto *copiedGroup = destination.getOrInsertComdat(group->getName());
			copiedGroup->setSelectionKind(group->getSelectionKind());
			copy.setComdat(copiedGroup);
		}
	}

	void copyAttachments(llvm::GlobalObject &copy, const llvm::GlobalObject &original) {
		llvm::SmallVector<std::pair<unsigned, llvm::MDNode *>, 4> metadata;
		original.getAllMetadata(metadata);
		for (const auto &[kind, node] : metadata)
			copy.addMetadata(kind, *llvm::MapMetadata(node, values, llvm::RF_None, nullptr, this));
	}

	llvm::GlobalValue *declare(const llvm::GlobalValue &original) {
		if (auto found = values.find(&original); found != values.end())
			return llvm::cast<llvm::GlobalValue>(found->second);
		bool definition = definitions.contains(&original);
		llvm::GlobalValue *copy;
		if (const auto *function = llvm::dyn_cast<llvm::Function>(&original)) {
			auto *declared = llvm::Function::Create(
				function->getFunctionType(),
				definition || function->isDeclaration() ? function->getLinkage() : llvm::GlobalValue::ExternalLinkage,
				function->getAddressSpace(), function->getName(), &destination
			);
			declared->copyAttributesFrom(function);
			if (!definition)
				declared->setPersonalityFn(nullptr);
			copy = declared;
		} else if (const auto *global = llvm::dyn_cast<llvm::GlobalVariable>(&original)) {
			auto *declared = new llvm::GlobalVariable(
				destination, global->getValueType(), global->isConstant(),
				definition || global->isDeclaration() ? global->getLinkage() : llvm::GlobalValue::ExternalLinkage, nullptr,
				global->getName(), nullptr, global->getThreadLocalMode(), global->getAddressSpace()
			);
			declared->copyAttributesFrom(global);
			copy = declared;
		} else if (definition && llvm::isa<llvm::GlobalAlias>(original)) {
			auto *alias = llvm::GlobalAlias::create(
				original.getValueType(), original.getAddressSpace(), original.getLinkage(), original.getName(), &destination
			);
			alias->copyAttributesFrom(llvm::cast<llvm::GlobalAlias>(&original));
			copy = alias;
		} else if (definition && llvm::isa<llvm::GlobalIFunc>(original)) {
			auto *ifunc = llvm::GlobalIFunc::create(
				original.getValueType(), original.getAddressSpace(), original.getLinkage(), original.getName(), nullptr,
				&destination
			);
			ifunc->copyAttributesFrom(llvm::cast<llvm::GlobalIFunc>(&original));
			copy = ifunc;
		} else if (original.getValueType()->isFunctionTy()) {
			// LLVM aliases and indirect functions have no declaration form.
			copy = llvm::Function::Create(
				llvm::cast<llvm::FunctionType>(original.getValueType()), llvm::GlobalValue::ExternalLinkage,
				original.getAddressSpace(), original.getName(), &destination
			);
		} else {
			const auto &alias = llvm::cast<llvm::GlobalAlias>(original);
			copy = new llvm::GlobalVariable(
				destination, alias.getValueType(), false, llvm::GlobalValue::ExternalLinkage, nullptr, alias.getName(), nullptr,
				alias.getThreadLocalMode(), alias.getAddressSpace()
			);
		}
		values[&original] = copy;
		pendingInterfaces.push(&original);
		if (definition)
			work.definitions++;
		else
			work.declarations++;
		return copy;
	}

	llvm::Value *materialize(llvm::Value *value) override {
		if (const auto *global = llvm::dyn_cast<llvm::GlobalValue>(value))
			return declare(*global);
		return nullptr; // LLVM maps local values and composite constants itself.
	}

	void finishInterfaces() {
		while (!pendingInterfaces.empty()) {
			const llvm::GlobalValue *original = pendingInterfaces.front();
			pendingInterfaces.pop();
			auto *copy = llvm::cast<llvm::GlobalValue>(values.lookup(original));
			if (const auto *function = llvm::dyn_cast<llvm::Function>(original)) {
				auto *declared = llvm::cast<llvm::Function>(copy);
				if (!definitions.contains(original)) {
					if (function->isDeclaration())
						copyAttachments(*declared, *function);
					if (function->hasPrefixData())
						declared->setPrefixData(
							llvm::MapValue(function->getPrefixData(), values, llvm::RF_None, nullptr, this)
						);
					if (function->hasPrologueData())
						declared->setPrologueData(
							llvm::MapValue(function->getPrologueData(), values, llvm::RF_None, nullptr, this)
						);
				}
			} else if (const auto *global = llvm::dyn_cast<llvm::GlobalVariable>(original)) {
				copyAttachments(*llvm::cast<llvm::GlobalVariable>(copy), *global);
			}
		}
	}

  public:
	PartitionMaterializer(
		llvm::Module &destination, std::span<const llvm::GlobalValue *const> definitions, ModulePartitionCloneWork &work
	)
		: destination(destination), definitions(definitions.begin(), definitions.end()), work(work) {
		for (const llvm::GlobalValue *value : definitions) {
			requireCompilerInvariant(!value->isDeclaration(), "partition requested a declaration as a definition");
			declare(*value);
		}
	}

	void clone(const llvm::Module &source, std::span<const llvm::GlobalValue *const> selected) {
		// Functions establish the basic-block map before block-address constants
		// in other definitions are remapped. Prototypes already break cycles.
		for (const llvm::GlobalValue *value : selected) {
			const auto *function = llvm::dyn_cast<llvm::Function>(value);
			if (!function)
				continue;
			auto *copy = llvm::cast<llvm::Function>(values.lookup(function));
			auto argument = copy->arg_begin();
			for (const llvm::Argument &original : function->args()) {
				argument->setName(original.getName());
				values[&original] = &*argument++;
			}
			llvm::SmallVector<llvm::ReturnInst *, 4> returns;
			llvm::CloneFunctionInto(
				copy, function, values, llvm::CloneFunctionChangeType::ClonedModule, returns, "", nullptr, nullptr, this
			);
			copyComdat(*copy, *function);
		}
		for (const llvm::GlobalValue *value : selected) {
			if (const auto *global = llvm::dyn_cast<llvm::GlobalVariable>(value)) {
				auto *copy = llvm::cast<llvm::GlobalVariable>(values.lookup(value));
				copy->setInitializer(llvm::MapValue(global->getInitializer(), values, llvm::RF_None, nullptr, this));
				copyComdat(*copy, *global);
			} else if (const auto *alias = llvm::dyn_cast<llvm::GlobalAlias>(value)) {
				llvm::cast<llvm::GlobalAlias>(values.lookup(value))
					->setAliasee(llvm::MapValue(alias->getAliasee(), values, llvm::RF_None, nullptr, this));
			} else if (const auto *ifunc = llvm::dyn_cast<llvm::GlobalIFunc>(value)) {
				llvm::cast<llvm::GlobalIFunc>(values.lookup(value))
					->setResolver(llvm::MapValue(ifunc->getResolver(), values, llvm::RF_None, nullptr, this));
			}
		}
		for (const llvm::NamedMDNode &named : source.named_metadata()) {
			auto *copy = destination.getOrInsertNamedMetadata(named.getName());
			for (const llvm::MDNode *node : named.operands())
				copy->addOperand(llvm::MapMetadata(node, values, llvm::RF_None, nullptr, this));
		}
		finishInterfaces();
	}
};
} // namespace

std::unique_ptr<llvm::Module> cloneModulePartition(
	const llvm::Module &source, std::span<const llvm::GlobalValue *const> definitions, ModulePartitionCloneWork &work
) {
	auto destination = std::make_unique<llvm::Module>(source.getModuleIdentifier(), source.getContext());
	destination->setSourceFileName(source.getSourceFileName());
	destination->setDataLayout(source.getDataLayout());
	destination->setTargetTriple(source.getTargetTriple());
	destination->setModuleInlineAsm(source.getModuleInlineAsm());
	PartitionMaterializer(*destination, definitions, work).clone(source, definitions);
	return destination;
}
