#include "incrementalIRReuse.h"
#include "codegen/debugTypeProducts.h"
#include "codegen/libraryDependencies.h"
#include "incrementalDebugIR.h"
#include "incrementalModuleBitcode.h"
#include "parseContext.h"
#include "section.h"
#include "variableStorage.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Linker/Linker.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/ValueMapper.h"
#include <optional>
#include <set>
#include <stdexcept>
#include <vector>

namespace {
void collectDependencies(
	const llvm::Value *value, const llvm::Function &owner, std::unordered_set<const llvm::Value *> &visited,
	std::unordered_set<const llvm::Function *> &dependencies, std::unordered_set<const llvm::GlobalVariable *> &mutableGlobals
) {
	if (!visited.insert(value).second)
		return;
	if (const auto *global = llvm::dyn_cast<llvm::GlobalVariable>(value)) {
		if (!global->isConstant())
			mutableGlobals.insert(global);
		if (global->hasInitializer())
			collectDependencies(global->getInitializer(), owner, visited, dependencies, mutableGlobals);
		return;
	}
	if (const auto *function = llvm::dyn_cast<llvm::Function>(value)) {
		if (function != &owner && !function->isDeclaration())
			dependencies.insert(function);
		return;
	}
	if (const auto *user = llvm::dyn_cast<llvm::User>(value)) {
		for (const llvm::Use &operand : user->operands())
			collectDependencies(operand.get(), owner, visited, dependencies, mutableGlobals);
	}
}

void collectDependencies(
	const llvm::Function &function, std::unordered_set<const llvm::Function *> &dependencies,
	std::unordered_set<const llvm::GlobalVariable *> &mutableGlobals
) {
	std::unordered_set<const llvm::Value *> visited;
	for (const llvm::BasicBlock &block : function) {
		for (const llvm::Instruction &instruction : block)
			collectDependencies(&instruction, function, visited, dependencies, mutableGlobals);
	}
}
} // namespace

IncrementalIRReuse::IncrementalIRReuse(
	ParseContext &context, const std::string &previousIR, const nlohmann::json &previousFunctions,
	const nlohmann::json &previousCallableFunctions, const nlohmann::json &previousFunctionInstantiations,
	const nlohmann::json *previousDebugInputs, std::map<const Section *, std::string> sectionPaths,
	std::shared_ptr<IncrementalUnitSelection> eligible, std::shared_ptr<IncrementalUnitSelection> replacements,
	GlobalInputCheck globalInputCheck
)
	: currentModule(context.llvmModule), previousFunctions(previousFunctions),
	  previousCallableFunctions(previousCallableFunctions), previousFunctionInstantiations(previousFunctionInstantiations),
	  previousDebugInputs(previousDebugInputs), sectionPaths(std::move(sectionPaths)), eligible(std::move(eligible)),
	  replacements(std::move(replacements)), globalInputCheck(std::move(globalInputCheck)) {
	previousModule = parseCachedModule(previousIR, *context.llvmContext);
	if (context.options.emitDebugInfo)
		debugInputs = std::make_unique<IncrementalDebugIR>(context, this->sectionPaths);
	if (previousModule->getDataLayout() != context.llvmModule->getDataLayout() ||
		previousModule->getTargetTriple() != context.llvmModule->getTargetTriple())
		throw std::runtime_error("cached LLVM target differs from the current target");
	for (const auto &[section, path] : this->sectionPaths)
		sectionsByPath.emplace(path, section);
	for (const auto &[name, key] : previousFunctionInstantiations.items()) {
		requireCompilerInvariant(previousFunctions.contains(name), "cached instantiation has no generated function owner");
		previousNamesByPathAndKey[previousFunctions.at(name).get<std::string>()][key.get<std::string>()].push_back(name);
	}
}

size_t IncrementalIRReuse::debugLocationQueries() const { return debugInputs ? debugInputs->queries() : 0; }
size_t IncrementalIRReuse::debugLocationHits() const { return debugInputs ? debugInputs->hits() : 0; }

bool IncrementalIRReuse::debugLocationsUnchanged(const std::string &name) const {
	if (!debugInputs)
		return true;
	auto [proof, inserted] = debugLocationProofs.try_emplace(name);
	if (inserted)
		proof->second = debugInputs->unchanged(previousDebugInputs->at(name));
	return proof->second;
}

bool IncrementalIRReuse::selected(const std::string &name, const IncrementalUnitSelection &selection) const {
	const std::string &path = previousFunctions.at(name).get_ref<const std::string &>();
	auto section = sectionsByPath.find(path);
	return section != sectionsByPath.end() &&
		   selection.contains(section->second, previousFunctionInstantiations.at(name).get<std::string>());
}

const IncrementalIRReuse::Dependencies &IncrementalIRReuse::dependenciesOf(const llvm::Function &function) const {
	auto [found, inserted] = dependencyCache.try_emplace(&function);
	if (inserted)
		collectDependencies(function, found->second.functions, found->second.mutableGlobals);
	else
		dependencyCacheHits++;
	return found->second;
}

bool IncrementalIRReuse::reusableClosure(
	const llvm::Function &function, llvm::Module &current, std::unordered_set<std::string> &names,
	const GlobalInputCheck &globalInputsUnchanged, bool allowDeferredGlobals
) const {
	std::string name = function.getName().str();
	if (!previousFunctions.contains(name))
		return false;
	bool replaced = selected(name, *replacements);
	bool stableWrapper = replaced && previousCallableFunctions.contains(name);
	if (replaced && !stableWrapper) {
		llvm::Function *now = current.getFunction(name);
		return !now || now->getFunctionType() == function.getFunctionType();
	}
	if (!names.insert(name).second)
		return true;
	if (!selected(name, *eligible) && !stableWrapper)
		return false;
	if (!debugLocationsUnchanged(name))
		return false;
	if (!globalInputsUnchanged(
			sectionsByPath.at(previousFunctions.at(name).get<std::string>()),
			previousFunctionInstantiations.at(name).get<std::string>()
		))
		return false;
	if (llvm::Function *now = current.getFunction(name); now && now->getFunctionType() != function.getFunctionType())
		return false;
	const Dependencies &dependencies = dependenciesOf(function);
	for (const llvm::GlobalVariable *old : dependencies.mutableGlobals) {
		llvm::GlobalVariable *now = current.getGlobalVariable(old->getName(), true);
		if (!now && allowDeferredGlobals)
			continue;
		if (allowDeferredGlobals && reusedRoot && now && now->isDeclaration() &&
			rootGlobalDeclarations.contains(old->getName().str()) && now->getValueType() == old->getValueType() &&
			old->hasInitializer())
			continue; // The validated cached entry supplies this prepared declaration's definition.
		if (!now || now->getValueType() != old->getValueType() || now->getLinkage() != old->getLinkage() ||
			!now->hasInitializer() || !old->hasInitializer() || now->getInitializer() != old->getInitializer())
			return false;
	}
	for (const llvm::Function *dependency : dependencies.functions) {
		if (!reusableClosure(*dependency, current, names, globalInputsUnchanged, allowDeferredGlobals))
			return false;
	}
	return true;
}

bool IncrementalIRReuse::hasReusableIRForInstantiation(const Section *section, const std::string &key) const {
	if (!eligible->contains(section, key) || replacements->contains(section, key))
		return false;
	const std::string &path = sectionPaths.at(section);
	auto owner = previousNamesByPathAndKey.find(path);
	if (owner == previousNamesByPathAndKey.end())
		return false;
	auto names = owner->second.find(key);
	if (names == owner->second.end())
		return false;
	std::map<std::pair<const Section *, std::string>, bool> globalsValid;
	auto validateGlobals = [&](const Section *owner, const std::string &identity) {
		auto [entry, inserted] = globalsValid.try_emplace({owner, identity});
		if (inserted) {
			deferredGlobalChecks++;
			entry->second = globalInputCheck(owner, identity);
			if (entry->second)
				deferredGlobalHits++;
		}
		return entry->second;
	};
	for (const std::string &name : names->second) {
		const llvm::Function *function = previousModule->getFunction(name);
		if (!function || function->isDeclaration())
			return false;
		std::unordered_set<std::string> closure;
		if (!reusableClosure(*function, *currentModule, closure, validateGlobals, true))
			return false;
	}
	return true;
}

bool IncrementalIRReuse::reuse(
	Section *section, llvm::Function *function, const nlohmann::json &currentFunctionInstantiations
) {
	std::string name = function->getName().str();
	if (!previousFunctions.contains(name))
		return false;
	auto sectionPath = sectionPaths.find(section);
	if (sectionPath == sectionPaths.end() || previousFunctions.at(name) != sectionPath->second)
		return false;
	const auto &currentKey = currentFunctionInstantiations.at(name);
	bool stableWrapper =
		previousCallableFunctions.contains(name) && replacements->contains(section, currentKey.get<std::string>());
	if (replacements->contains(section, currentKey.get<std::string>()) && !stableWrapper)
		return false;
	if ((!eligible->contains(section, currentKey.get<std::string>()) && !stableWrapper) ||
		previousFunctionInstantiations.at(name) != currentKey)
		return false;
	llvm::Function *old = previousModule->getFunction(name);
	if (!old || old->isDeclaration() || old->getFunctionType() != function->getFunctionType())
		return false;
	std::unordered_set<std::string> names;
	if (!reusableClosure(*old, *function->getParent(), names, globalInputCheck, reusedRoot))
		return false;
	reusedNames.insert(names.begin(), names.end());
	return true;
}

bool IncrementalIRReuse::reuseRoot() {
	if (!debugLocationsUnchanged("main"))
		return false;
	const llvm::Function *entry = previousModule->getFunction("main");
	requireCompilerInvariant(entry && !entry->isDeclaration(), "cached executable has no entry-point body");
	std::unordered_set<std::string> closure;
	for (const llvm::Function *dependency : dependenciesOf(*entry).functions)
		if (!reusableClosure(*dependency, *currentModule, closure, [](const auto *, const auto &) {
			return true;
		}, true))
			return false;
	reusedNames.insert(closure.begin(), closure.end());
	reusedRoot = true;
	return true;
}

void IncrementalIRReuse::bindRootGlobals(ParseContext &context, const nlohmann::json &managedStorage) {
	requireCompilerInvariant(reusedRoot, "global storage import requires a validated entry point");
	std::map<std::string, std::set<VariableStorageKey>> definitions;
	std::map<std::string, DataType> types;
	for (const auto &[section, path] : sectionPaths)
		for (const auto &[name, variable] : section->variables)
			if (variable->isGlobal && !variable->dataInitializer) {
				definitions[name].insert(variableStorageKey(variable->definition, nullptr));
				auto [entry, inserted] = types.emplace(name, variable->type);
				requireCompilerInvariant(
					inserted || entry->second == variable->type, "global storage aliases disagree on their type"
				);
			}
	auto declare = [&](const llvm::GlobalVariable &old) {
		auto *declaration = new llvm::GlobalVariable(
			*currentModule, old.getValueType(), old.isConstant(), llvm::GlobalValue::ExternalLinkage, nullptr, old.getName()
		);
		declaration->copyAttributesFrom(&old);
		declaration->setLinkage(llvm::GlobalValue::ExternalLinkage);
		rootGlobalDeclarations.insert(declaration->getName().str());
		return declaration;
	};
	for (const llvm::GlobalVariable &old : previousModule->globals()) {
		auto found = definitions.find(old.getName().str());
		if (found == definitions.end())
			continue; // Runtime-internal globals have no source storage declaration.
		requireCompilerInvariant(!old.isDeclaration(), "entry-owned source global has no definition");
		auto *declaration = declare(old);
		context.globalLLVMVariables.emplace(old.getName().str(), declaration);
		for (const auto &key : found->second)
			context.variableStorage.emplace(key, declaration);
	}
	for (const auto &[name, flagName] : managedStorage.items()) {
		auto *flag = previousModule->getGlobalVariable(flagName.get<std::string>(), true);
		requireCompilerInvariant(flag && !flag->isDeclaration(), "managed global lost its initialization state");
		context.managedGlobalStorage.push_back({context.globalLLVMVariables.at(name), declare(*flag), types.at(name), nullptr});
	}
}

bool IncrementalIRReuse::import(ParseContext &context, const nlohmann::json &currentFunctionInstantiations) {
	if (reusedNames.empty() && !reusedRoot)
		return true;
	std::vector<std::string> names(reusedNames.begin(), reusedNames.end());
	if (reusedRoot)
		names.emplace_back("main");
	std::set<std::string> mutableGlobalNames;
	for (const std::string &name : names) {
		const Dependencies &dependencies = dependenciesOf(*previousModule->getFunction(name));
		for (const llvm::GlobalVariable *global : dependencies.mutableGlobals)
			mutableGlobalNames.insert(global->getName().str());
		for (const llvm::Function *dependency : dependencies.functions) {
			std::string dependencyName = dependency->getName().str();
			if (!previousFunctions.contains(dependencyName) || !selected(dependencyName, *replacements) ||
				previousCallableFunctions.contains(dependencyName))
				continue;
			llvm::Function *replacement = context.llvmModule->getFunction(dependencyName);
			auto key = currentFunctionInstantiations.find(dependencyName);
			if (!replacement || replacement->isDeclaration() ||
				replacement->getFunctionType() != dependency->getFunctionType() || key == currentFunctionInstantiations.end() ||
				*key != previousFunctionInstantiations.at(dependencyName))
				throw std::runtime_error("reused caller lost its rebuilt callee: " + dependencyName);
		}
	}
	llvm::ValueToValueMapTy values;
	std::vector<llvm::MDNode *> retainedTypeProducts;
	if (context.diBuilder) {
		std::set<const llvm::DICompositeType *> usedTypes;
		for (const std::string &name : names)
			IncrementalDebugIR::visitMetadata(*previousModule->getFunction(name), [&](const llvm::MDNode *metadata) {
				if (const auto *type = llvm::dyn_cast<llvm::DICompositeType>(metadata))
					usedTypes.insert(type);
			});
		std::map<std::string, llvm::DICompositeType *> currentTypes;
		auto *currentProducts = currentModule->getOrInsertNamedMetadata(debugTypeProductsName);
		for (llvm::MDNode *record : currentProducts->operands())
			currentTypes.emplace(
				llvm::cast<llvm::MDString>(record->getOperand(0))->getString().str(),
				llvm::cast<llvm::DICompositeType>(record->getOperand(1))
			);
		if (auto *previousProducts = previousModule->getNamedMetadata(debugTypeProductsName))
			for (llvm::MDNode *record : previousProducts->operands()) {
				auto *type = llvm::cast<llvm::DICompositeType>(record->getOperand(1));
				if (!usedTypes.contains(type))
					continue;
				std::string identity = llvm::cast<llvm::MDString>(record->getOperand(0))->getString().str();
				auto current = currentTypes.find(identity);
				if (current != currentTypes.end())
					values.MD()[type] = llvm::TrackingMDRef(current->second);
				else
					retainedTypeProducts.push_back(record);
			}
		for (llvm::DICompileUnit *unit : previousModule->debug_compile_units())
			values.MD()[unit] = llvm::TrackingMDRef(context.diCompileUnit);
		std::vector<llvm::Metadata *> globals;
		for (auto *global : context.diCompileUnit->getGlobalVariables())
			globals.push_back(global);
		for (llvm::GlobalVariable &global : previousModule->globals()) {
			if (!reusedRoot && !mutableGlobalNames.contains(global.getName().str()))
				continue;
			llvm::SmallVector<llvm::DIGlobalVariableExpression *, 1> oldInfo, currentInfo;
			global.getDebugInfo(oldInfo);
			if (auto *current = currentModule->getGlobalVariable(global.getName(), true))
				current->getDebugInfo(currentInfo);
			for (llvm::DIGlobalVariableExpression *old : oldInfo) {
				llvm::DIGlobalVariableExpression *replacement;
				if (!currentInfo.empty()) {
					requireCompilerInvariant(currentInfo.size() == 1, "generated global has multiple debug declarations");
					replacement = currentInfo.front();
				} else {
					auto *variable = old->getVariable();
					auto *type = llvm::cast<llvm::DIType>(llvm::MapMetadata(variable->getType(), values));
					auto *declaration = llvm::DIGlobalVariable::getDistinct(
						*context.llvmContext, context.diCompileUnit, variable->getName(), variable->getLinkageName(),
						variable->getFile(), variable->getLine(), type, variable->isLocalToUnit(), variable->isDefinition(),
						variable->getStaticDataMemberDeclaration(), variable->getTemplateParams(), variable->getAlignInBits(),
						variable->getAnnotations()
					);
					replacement =
						llvm::DIGlobalVariableExpression::get(*context.llvmContext, declaration, old->getExpression());
					globals.push_back(replacement);
				}
				values.MD()[old] = llvm::TrackingMDRef(replacement);
			}
		}
		context.diCompileUnit->replaceGlobalVariables(llvm::MDTuple::get(*context.llvmContext, globals));
	}
	std::unique_ptr<llvm::Module> cached = llvm::CloneModule(*previousModule, values, [&](const llvm::GlobalValue *value) {
		if (const auto *function = llvm::dyn_cast<llvm::Function>(value))
			return reusedNames.contains(function->getName().str()) || (reusedRoot && function->getName() == "main");
		return true;
	});
	if (auto *products = cached->getNamedMetadata(debugTypeProductsName))
		cached->eraseNamedMetadata(products);
	for (llvm::MDNode *record : retainedTypeProducts)
		currentModule->getOrInsertNamedMetadata(debugTypeProductsName)->addOperand(llvm::MapMetadata(record, values));
	for (const std::string &name : names)
		cached->getFunction(name)->setLinkage(llvm::GlobalValue::ExternalLinkage);
	std::vector<std::pair<std::string, llvm::GlobalValue::LinkageTypes>> globalLinkages;
	for (const std::string &name : mutableGlobalNames) {
		llvm::GlobalVariable *old = cached->getGlobalVariable(name, true);
		llvm::GlobalVariable *now = context.llvmModule->getGlobalVariable(name, true);
		requireCompilerInvariant(old, "cached function references a missing global variable");
		if (!now && reusedRoot)
			continue;
		if (!now)
			throw std::runtime_error("reused function lost a global variable: " + name);
		globalLinkages.emplace_back(name, now->isDeclaration() ? old->getLinkage() : now->getLinkage());
		if (!now->isDeclaration())
			old->setInitializer(nullptr);
		old->setLinkage(llvm::GlobalValue::ExternalLinkage);
		now->setLinkage(llvm::GlobalValue::ExternalLinkage);
	}
	std::vector<std::pair<std::string, llvm::GlobalValue::LinkageTypes>> replacementLinkages;
	for (const auto &[name, owner] : previousFunctions.items()) {
		if (!selected(name, *replacements) || previousCallableFunctions.contains(name))
			continue;
		if (llvm::Function *declaration = cached->getFunction(name)) {
			requireCompilerInvariant(declaration->isDeclaration(), "incremental replacement retained an old function body");
			declaration->setLinkage(llvm::GlobalValue::ExternalLinkage);
		}
		llvm::Function *function = context.llvmModule->getFunction(name);
		if (!function || function->isDeclaration())
			continue;
		replacementLinkages.emplace_back(name, function->getLinkage());
		function->setLinkage(llvm::GlobalValue::ExternalLinkage);
	}
	if (llvm::Linker::linkModules(*context.llvmModule, std::move(cached), llvm::Linker::Flags::LinkOnlyNeeded))
		throw std::runtime_error("failed to import cached function IR");
	for (const auto &[name, linkage] : globalLinkages)
		context.llvmModule->getGlobalVariable(name, true)->setLinkage(linkage);
	for (const auto &[name, linkage] : replacementLinkages)
		context.llvmModule->getFunction(name)->setLinkage(linkage);
	for (const std::string &name : names) {
		llvm::Function *function = context.llvmModule->getFunction(name);
		if (!function || function->isDeclaration())
			throw std::runtime_error("cached function IR was not linked: " + name);
		function->setLinkage(previousModule->getFunction(name)->getLinkage());
		restoreRequiredLibraries(context, *function);
	}
	if (reusedRoot)
		context.mainLLVMFunction = context.llvmModule->getFunction("main");
	return true;
}
