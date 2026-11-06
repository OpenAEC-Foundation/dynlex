#pragma once

#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_set>

namespace llvm {
class Module;
class Function;
} // namespace llvm
struct ParseContext;
struct Section;
struct Instantiation;

class IncrementalIRReuse {
  public:
	IncrementalIRReuse(
		ParseContext &context, const std::string &previousIR, const nlohmann::json &previousFunctions,
		std::map<const Section *, std::string> sectionPaths, std::unordered_set<const Section *> eligibleSections
	);
	bool reuse(Section *section, Instantiation &instantiation, llvm::Function *function);
	bool import(ParseContext &context);
	size_t reusedCount() const { return reusedNames.size(); }
	const std::unordered_set<std::string> &reusedFunctionNames() const { return reusedNames; }

  private:
	bool reusableClosure(const llvm::Function &function, llvm::Module &current, std::unordered_set<std::string> &names) const;
	std::unique_ptr<llvm::Module> previousModule;
	const nlohmann::json &previousFunctions;
	std::map<const Section *, std::string> sectionPaths;
	std::unordered_set<const Section *> eligibleSections;
	std::unordered_set<std::string> eligiblePaths;
	std::unordered_set<std::string> reusedNames;
};
