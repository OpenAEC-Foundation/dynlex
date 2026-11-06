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
#include <stdexcept>

namespace {
bool collectDependencies(
	const llvm::Value *value, const llvm::Function &owner, std::unordered_set<const llvm::Value *> &visited,
	std::unordered_set<const llvm::Function *> &dependencies
) {
	if (!visited.insert(value).second)
		return false;
	if (const auto *global = llvm::dyn_cast<llvm::GlobalVariable>(value))
		return !global->isConstant();
	if (const auto *function = llvm::dyn_cast<llvm::Function>(value)) {
		if (function != &owner && !function->isDeclaration())
			dependencies.insert(function);
		return false;
	}
	if (const auto *user = llvm::dyn_cast<llvm::User>(value)) {
		for (const llvm::Use &operand : user->operands()) {
			if (collectDependencies(operand.get(), owner, visited, dependencies))
				return true;
		}
	}
	return false;
}

bool collectDependencies(const llvm::Function &function, std::unordered_set<const llvm::Function *> &dependencies) {
	std::unordered_set<const llvm::Value *> visited;
	for (const llvm::BasicBlock &block : function) {
		for (const llvm::Instruction &instruction : block) {
			if (collectDependencies(&instruction, function, visited, dependencies))
				return true;
		}
	}
	return false;
}
} // namespace

IncrementalIRReuse::IncrementalIRReuse(
	ParseContext &context, const std::string &previousIR, const nlohmann::json &previousFunctions,
	std::map<const Section *, std::string> sectionPaths, std::unordered_set<const Section *> eligibleSections
)
	: currentModule(context.llvmModule), previousFunctions(previousFunctions), sectionPaths(std::move(sectionPaths)),
	  eligibleSections(std::move(eligibleSections)) {
	llvm::SMDiagnostic diagnostic;
	previousModule = llvm::parseAssemblyString(previousIR, diagnostic, *context.llvmContext);
	if (!previousModule)
		throw std::runtime_error("invalid cached LLVM IR for function reuse");
	if (previousModule->getDataLayout() != context.llvmModule->getDataLayout() ||
		previousModule->getTargetTriple() != context.llvmModule->getTargetTriple())
		throw std::runtime_error("cached LLVM target differs from the current target");
	for (const Section *section : this->eligibleSections)
		eligiblePaths.insert(this->sectionPaths.at(section));
}

bool IncrementalIRReuse::reusableClosure(
	const llvm::Function &function, llvm::Module &current, std::unordered_set<std::string> &names
) const {
	std::string name = function.getName().str();
	if (!names.insert(name).second)
		return true;
	if (!previousFunctions.contains(name) || !eligiblePaths.contains(previousFunctions.at(name).get<std::string>()))
		return false;
	if (llvm::Function *now = current.getFunction(name); now && now->getFunctionType() != function.getFunctionType())
		return false;
	std::unordered_set<const llvm::Function *> dependencies;
	if (collectDependencies(function, dependencies))
		return false;
	for (const llvm::Function *dependency : dependencies) {
		if (!reusableClosure(*dependency, current, names))
			return false;
	}
	return true;
}

bool IncrementalIRReuse::hasReusableIRForSection(const Section *section) const {
	if (!eligibleSections.contains(section))
		return false;
	const std::string &path = sectionPaths.at(section);
	bool found = false;
	for (const auto &[name, owner] : previousFunctions.items()) {
		if (owner != path)
			continue;
		const llvm::Function *function = previousModule->getFunction(name);
		if (!function || function->isDeclaration())
			return false;
		std::unordered_set<std::string> closure;
		if (!reusableClosure(*function, *currentModule, closure))
			return false;
		found = true;
	}
	return found;
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
	llvm::ValueToValueMapTy values;
	std::unique_ptr<llvm::Module> cached = llvm::CloneModule(*previousModule, values, [&](const llvm::GlobalValue *value) {
		if (const auto *function = llvm::dyn_cast<llvm::Function>(value))
			return reusedNames.contains(function->getName().str());
		return true;
	});
	for (const std::string &name : reusedNames)
		cached->getFunction(name)->setLinkage(llvm::GlobalValue::ExternalLinkage);
	if (llvm::Linker::linkModules(*context.llvmModule, std::move(cached), llvm::Linker::Flags::LinkOnlyNeeded))
		throw std::runtime_error("failed to import cached function IR");
	for (const std::string &name : reusedNames) {
		llvm::Function *function = context.llvmModule->getFunction(name);
		if (!function || function->isDeclaration())
			throw std::runtime_error("cached function IR was not linked: " + name);
		function->setLinkage(llvm::GlobalValue::InternalLinkage);
		restoreRequiredLibraries(context, *function);
	}
	return true;
}
