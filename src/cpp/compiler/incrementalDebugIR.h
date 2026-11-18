#pragma once
#include <functional>
#include <map>
#include <nlohmann/json.hpp>
#include <string>

struct ParseContext;
struct Section;
namespace llvm {
class Function;
class MDNode;
class Module;
} // namespace llvm

class IncrementalDebugIR {
  public:
	IncrementalDebugIR(const ParseContext &context, const std::map<const Section *, std::string> &paths);
	nlohmann::json capture(const llvm::Function &function) const;
	bool unchanged(const nlohmann::json &inputs) const;
	static void visitMetadata(const llvm::Function &, const std::function<void(const llvm::MDNode *)> &);
	size_t queries() const { return checked; }
	size_t hits() const { return reused; }

  private:
	std::map<std::pair<std::string, unsigned>, nlohmann::json> authored;
	std::map<std::string, nlohmann::json> locations;
	mutable size_t checked = 0, reused = 0;
};

nlohmann::json captureDebugIRInputs(const ParseContext &, const std::map<const Section *, std::string> &);
