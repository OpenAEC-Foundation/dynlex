#include "incrementalIRReuse.h"
#include "codegen/libraryDependencies.h"
#include "parseContext.h"
#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Linker/Linker.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Transforms/Utils/Cloning.h"
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
	const nlohmann::json &previousCallableFunctions, std::map<const Section *, std::string> sectionPaths,
	std::unordered_set<const Section *> eligibleSections, std::unordered_set<std::string> replacementPaths
)
	: currentModule(context.llvmModule), previousFunctions(previousFunctions),
	  previousCallableFunctions(previousCallableFunctions), sectionPaths(std::move(sectionPaths)),
	  eligibleSections(std::move(eligibleSections)), replacementPaths(std::move(replacementPaths)) {
	llvm::SMDiagnostic diagnostic;
	previousModule = llvm::parseAssemblyString(previousIR, diagnostic, *context.llvmContext);
	if (!previousModule)
		throw std::runtime_error("invalid cached LLVM IR for function reuse");
	if (previousModule->getDataLayout() != context.llvmModule->getDataLayout() ||
		previousModule->getTargetTriple() != context.llvmModule->getTargetTriple())
		throw std::runtime_error("cached LLVM target differs from the current target");
	for (const Section *section : this->eligibleSections)
		eligiblePaths.insert(this->sectionPaths.at(section));
	for (const auto &[name, owner] : previousFunctions.items())
		previousNamesByPath[owner.get<std::string>()].push_back(name);
}

bool IncrementalIRReuse::reusableClosure(
	const llvm::Function &function, llvm::Module &current, std::unordered_set<std::string> &names,
	bool allowUnmaterializedGlobals
) const {
	std::string name = function.getName().str();
	if (!previousFunctions.contains(name))
		return false;
	std::string owner = previousFunctions.at(name).get<std::string>();
	bool stableWrapper = replacementPaths.contains(owner) && previousCallableFunctions.contains(name);
	if (replacementPaths.contains(owner) && !stableWrapper) {
		llvm::Function *now = current.getFunction(name);
		return !now || now->getFunctionType() == function.getFunctionType();
	}
	if (!names.insert(name).second)
		return true;
	if (!eligiblePaths.contains(owner) && !stableWrapper)
		return false;
	if (llvm::Function *now = current.getFunction(name); now && now->getFunctionType() != function.getFunctionType())
		return false;
	std::unordered_set<const llvm::Function *> dependencies;
	std::unordered_set<const llvm::GlobalVariable *> mutableGlobals;
	collectDependencies(function, dependencies, mutableGlobals);
	for (const llvm::GlobalVariable *old : mutableGlobals) {
		llvm::GlobalVariable *now = current.getGlobalVariable(old->getName(), true);
		if (!now && allowUnmaterializedGlobals)
			continue;
		if (!now || now->getValueType() != old->getValueType() || now->getLinkage() != old->getLinkage() ||
			!now->hasInitializer() || !old->hasInitializer() || now->getInitializer() != old->getInitializer())
			return false;
	}
	for (const llvm::Function *dependency : dependencies) {
		if (!reusableClosure(*dependency, current, names, allowUnmaterializedGlobals))
			return false;
	}
	return true;
}

bool IncrementalIRReuse::hasReusableIRForSection(const Section *section, bool allowUnmaterializedGlobals) const {
	if (!eligibleSections.contains(section))
		return false;
	const std::string &path = sectionPaths.at(section);
	auto names = previousNamesByPath.find(path);
	if (names == previousNamesByPath.end())
		return false;
	for (const std::string &name : names->second) {
		const llvm::Function *function = previousModule->getFunction(name);
		if (!function || function->isDeclaration())
			return false;
		std::unordered_set<std::string> closure;
		if (!reusableClosure(*function, *currentModule, closure, allowUnmaterializedGlobals))
			return false;
	}
	return true;
}

bool IncrementalIRReuse::reuse(Section *section, Instantiation &, llvm::Function *function) {
	if (!eligibleSections.contains(section))
		return false;
	std::string name = function->getName().str();
	if (!previousFunctions.contains(name))
		return false;
	auto sectionPath = sectionPaths.find(section);
	if (sectionPath == sectionPaths.end() || previousFunctions.at(name) != sectionPath->second)
		return false;
	llvm::Function *old = previousModule->getFunction(name);
	if (!old || old->isDeclaration() || old->getFunctionType() != function->getFunctionType())
		return false;
	std::unordered_set<std::string> names;
	if (!reusableClosure(*old, *function->getParent(), names))
		return false;
	reusedNames.insert(names.begin(), names.end());
	return true;
}

bool IncrementalIRReuse::import(ParseContext &context) {
	if (reusedNames.empty())
		return true;
	for (const std::string &name : reusedNames) {
		std::unordered_set<const llvm::Function *> dependencies;
		std::unordered_set<const llvm::GlobalVariable *> globals;
		collectDependencies(*previousModule->getFunction(name), dependencies, globals);
		for (const llvm::Function *dependency : dependencies) {
			std::string dependencyName = dependency->getName().str();
			if (!previousFunctions.contains(dependencyName) ||
				!replacementPaths.contains(previousFunctions.at(dependencyName).get<std::string>()) ||
				previousCallableFunctions.contains(dependencyName))
				continue;
			llvm::Function *replacement = context.llvmModule->getFunction(dependencyName);
			if (!replacement || replacement->isDeclaration() || replacement->getFunctionType() != dependency->getFunctionType())
				throw std::runtime_error("reused caller lost its rebuilt callee: " + dependencyName);
		}
	}
	std::set<std::string> mutableGlobalNames;
	for (const std::string &name : reusedNames) {
		std::unordered_set<const llvm::Function *> dependencies;
		std::unordered_set<const llvm::GlobalVariable *> mutableGlobals;
		collectDependencies(*previousModule->getFunction(name), dependencies, mutableGlobals);
		for (const llvm::GlobalVariable *global : mutableGlobals)
			mutableGlobalNames.insert(global->getName().str());
	}
	llvm::ValueToValueMapTy values;
	std::unique_ptr<llvm::Module> cached = llvm::CloneModule(*previousModule, values, [&](const llvm::GlobalValue *value) {
		if (const auto *function = llvm::dyn_cast<llvm::Function>(value))
			return reusedNames.contains(function->getName().str());
		return true;
	});
	for (const std::string &name : reusedNames)
		cached->getFunction(name)->setLinkage(llvm::GlobalValue::ExternalLinkage);
	std::vector<std::pair<std::string, llvm::GlobalValue::LinkageTypes>> globalLinkages;
	for (const std::string &name : mutableGlobalNames) {
		llvm::GlobalVariable *old = cached->getGlobalVariable(name, true);
		llvm::GlobalVariable *now = context.llvmModule->getGlobalVariable(name, true);
		if (!old || !now)
			throw std::runtime_error("reused function lost a global variable: " + name);
		globalLinkages.emplace_back(name, now->getLinkage());
		old->setInitializer(nullptr);
		old->setLinkage(llvm::GlobalValue::ExternalLinkage);
		now->setLinkage(llvm::GlobalValue::ExternalLinkage);
	}
	std::vector<std::pair<std::string, llvm::GlobalValue::LinkageTypes>> replacementLinkages;
	for (const auto &[name, owner] : previousFunctions.items()) {
		if (!replacementPaths.contains(owner.get<std::string>()) || previousCallableFunctions.contains(name))
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
	for (const std::string &name : reusedNames) {
		llvm::Function *function = context.llvmModule->getFunction(name);
		if (!function || function->isDeclaration())
			throw std::runtime_error("cached function IR was not linked: " + name);
		function->setLinkage(previousModule->getFunction(name)->getLinkage());
		restoreRequiredLibraries(context, *function);
	}
	return true;
}
