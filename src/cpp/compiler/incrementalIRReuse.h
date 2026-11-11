#pragma once
#include "incrementalUnitSelection.h"

#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace llvm {
class Module;
class Function;
class GlobalVariable;
} // namespace llvm
struct ParseContext;
struct Section;
struct Instantiation;

class IncrementalIRReuse {
  public:
	IncrementalIRReuse(
		ParseContext &context, const std::string &previousIR, const nlohmann::json &previousFunctions,
		const nlohmann::json &previousCallableFunctions, const nlohmann::json &previousFunctionInstantiations,
		std::map<const Section *, std::string> sectionPaths, std::shared_ptr<IncrementalUnitSelection> eligible,
		std::shared_ptr<IncrementalUnitSelection> replacements
	);
	bool reuse(Section *section, llvm::Function *function, const nlohmann::json &currentFunctionInstantiations);
	bool hasReusableIRForInstantiation(
		const Section *section, const std::string &key, bool allowUnmaterializedGlobals = false
	) const;
	bool reuseRoot();
	bool rootReused() const { return reusedRoot; }
	bool import(ParseContext &context, const nlohmann::json &currentFunctionInstantiations);
	size_t reusedCount() const { return reusedNames.size(); }
	size_t dependencyScans() const { return dependencyCache.size(); }
	size_t dependencyHits() const { return dependencyCacheHits; }
	const std::unordered_set<std::string> &reusedFunctionNames() const { return reusedNames; }

  private:
	struct Dependencies {
		std::unordered_set<const llvm::Function *> functions;
		std::unordered_set<const llvm::GlobalVariable *> mutableGlobals;
	};
	const Dependencies &dependenciesOf(const llvm::Function &function) const;
	bool selected(const std::string &name, const IncrementalUnitSelection &selection) const;
	bool reusableClosure(
		const llvm::Function &function, llvm::Module &current, std::unordered_set<std::string> &names,
		bool allowUnmaterializedGlobals = false
	) const;
	std::unique_ptr<llvm::Module> previousModule;
	llvm::Module *currentModule;
	const nlohmann::json &previousFunctions;
	const nlohmann::json &previousCallableFunctions;
	const nlohmann::json &previousFunctionInstantiations;
	std::map<const Section *, std::string> sectionPaths;
	std::shared_ptr<IncrementalUnitSelection> eligible;
	std::shared_ptr<IncrementalUnitSelection> replacements;
	std::map<std::string, const Section *> sectionsByPath;
	std::unordered_map<std::string, std::unordered_map<std::string, std::vector<std::string>>> previousNamesByPathAndKey;
	std::unordered_set<std::string> reusedNames;
	bool reusedRoot = false;
	mutable std::unordered_map<const llvm::Function *, Dependencies> dependencyCache;
	mutable size_t dependencyCacheHits = 0;
};
