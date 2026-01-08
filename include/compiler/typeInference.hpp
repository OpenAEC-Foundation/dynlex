#pragma once

#include "compiler/inferredType.hpp"
#include "compiler/intrinsicInfo.hpp"
#include "compiler/patternResolver.hpp"
#include "compiler/typedCall.hpp"
#include "compiler/typedParameter.hpp"
#include "compiler/typedPattern.hpp"
#include "compiler/typedValue.hpp"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace tbx {

/**
 * TypeInference - Step 4 of the 3BX compiler pipeline
 *
 * Infers types for pattern parameters and return values based on intrinsic
 * usage.
 */
class TypeInference {
public:
  TypeInference();

  /**
   * Run type inference on resolved patterns
   * @return true if all types were successfully inferred (no errors)
   */
  bool infer(const SectionPatternResolver &resolver);

  /**
   * Get any diagnostics (errors/warnings) found during inference
   */
  const std::vector<Diagnostic> &diagnostics() const { return diagnosticsData; }

  /**
   * Get typed representation of a pattern
   */
  TypedPattern *getTypedPattern(ResolvedPattern *pattern) const {
    auto it = patternToTypedData.find(pattern);
    return it != patternToTypedData.end() ? it->second : nullptr;
  }

  /**
   * Get all typed patterns
   */
  const std::vector<std::unique_ptr<TypedPattern>> &typedPatterns() const {
    return typedPatternsData;
  }

  /**
   * Print results for debugging
   */
  void printResults() const;

private:
  std::unique_ptr<TypedPattern> inferPatternTypes(ResolvedPattern *pattern);
  std::unique_ptr<TypedCall> inferCallTypes(PatternMatch *match);

  std::vector<IntrinsicInfo> parseIntrinsics(const std::string &text);
  IntrinsicInfo parseSingleIntrinsic(const std::string &text, size_t startPos);

  InferredType inferValueType(const ResolvedValue &value);
  TypedValue resolvedToTyped(const ResolvedValue &value,
                             const std::string &varName);

  bool isCompatible(InferredType expected, InferredType actual) const;
  InferredType unifyTypes(InferredType t1, InferredType t2) const;

  std::vector<std::unique_ptr<TypedPattern>> typedPatternsData;
  std::vector<std::unique_ptr<TypedCall>> typedCallsData;
  std::map<ResolvedPattern *, TypedPattern *> patternToTypedData;
  std::vector<Diagnostic> diagnosticsData;
};

} // namespace tbx
