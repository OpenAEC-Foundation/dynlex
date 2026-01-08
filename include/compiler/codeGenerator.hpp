#pragma once

#include "compiler/codegenPattern.hpp"
#include "compiler/diagnostic.hpp"
#include "compiler/patternResolver.hpp"
#include "compiler/sectionAnalyzer.hpp"
#include "compiler/typeInference.hpp"

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Value.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace tbx {

/**
 * SectionCodeGenerator - Step 5 of the 3BX compiler pipeline
 *
 * Generates LLVM IR from resolved and typed patterns.
 *
 * Key mappings:
 * - @intrinsic("add", a, b)   -> add i64 %a, %b
 * - @intrinsic("sub", a, b)   -> sub i64 %a, %b
 * - @intrinsic("mul", a, b)   -> mul i64 %a, %b
 * - @intrinsic("div", a, b)   -> sdiv i64 %a, %b
 * - @intrinsic("print", v)    -> call printf
 * - @intrinsic("store", v, x) -> store i64 %x, i64* %v
 * - @intrinsic("load", v)     -> load i64, i64* %v
 * - @intrinsic("return", v)   -> ret i64 %v
 *
 * Pattern to function mapping:
 * - Each "effect" pattern becomes a void function
 * - Each "expression" pattern becomes a function returning the expression type
 * - Pattern variables become function parameters
 */
class SectionCodeGenerator {
public:
  /**
   * Construct a SectionCodeGenerator
   * @param moduleName Name for the generated LLVM module
   */
  explicit SectionCodeGenerator(const std::string &moduleName);

  /**
   * Generate LLVM IR from resolved patterns
   * @param resolver The pattern resolver containing resolved patterns
   * @param root The root section from section analysis
   * @return true if code generation succeeded
   */
  bool generate(SectionPatternResolver &resolver, Section *root);

  /**
   * Generate LLVM IR using existing type inference results
   * @param typeInference The type inference results from Step 4
   * @param resolver The pattern resolver containing resolved patterns
   * @param root The root section from section analysis
   * @return true if code generation succeeded
   */
  bool generate(const TypeInference &typeInference,
                SectionPatternResolver &resolver, Section *root);

  /**
   * Get the generated LLVM module
   */
  llvm::Module *getModule() { return module.get(); }

  /**
   * Take ownership of the module (for JIT execution)
   */
  std::unique_ptr<llvm::Module> takeModule() { return std::move(module); }

  /**
   * Take ownership of the context (for JIT execution)
   */
  std::unique_ptr<llvm::LLVMContext> takeContext() {
    return std::move(context);
  }

  /**
   * Output LLVM IR to file
   */
  bool writeIr(const std::string &filename);

  /**
   * Print LLVM IR to stdout
   */
  void printIr();

  /**
   * Get any errors that occurred during generation
   */
  const std::vector<Diagnostic> &diagnostics() const { return diagnosticsData; }

private:
  // =========================================================================
  // Type Handling
  // =========================================================================

  /**
   * Run type inference internally
   */
  void runTypeInference(SectionPatternResolver &resolver);

  /**
   * Convert InferredType enum to LLVM type
   */
  llvm::Type *typeToLlvm(InferredType type);

  /**
   * Infer return type from pattern type
   */
  InferredType inferReturnTypeFromPattern(const ResolvedPattern *pattern);

  // =========================================================================
  // Code Generation
  // =========================================================================

  /**
   * Generate external declarations (printf, etc.)
   */
  void generateExternalDeclarations();

  /**
   * Declare LLVM function signature for a pattern (no body)
   * This must be called for ALL patterns before generating bodies
   */
  void declarePatternFunction(CodegenPattern &codegenPattern);

  /**
   * Generate LLVM function body for a pattern
   * Requires all pattern functions to be declared first
   */
  void generatePatternFunctionBody(CodegenPattern &codegenPattern);

  /**
   * Generate main function from top-level pattern references
   */
  void generateMain(Section *root, SectionPatternResolver &resolver);

  /**
   * Generate code for a single code line
   */
  llvm::Value *generateCodeLine(CodeLine *line,
                                SectionPatternResolver &resolver);

  /**
   * Generate code for a line within a pattern body
   * Handles both intrinsic calls and pattern references
   * @param text The line text to process
   * @return The generated LLVM value
   */
  llvm::Value *generateBodyLine(const std::string &text);

  /**
   * Generate code for a pattern call (reference)
   */
  llvm::Value *generatePatternCall(PatternMatch *match);

  /**
   * Generate code for an intrinsic call
   * @param text The intrinsic call text (e.g., "@intrinsic(\"add\", left,
   * right)")
   * @param localVars Map of local variable names to their LLVM values
   * @return The generated LLVM value
   */
  llvm::Value *generateIntrinsic(
      const std::string &text,
      const std::unordered_map<std::string, llvm::Value *> &localVars);

  /**
   * Parse intrinsic call text into name and arguments
   * @param text The intrinsic call text
   * @param name Output: intrinsic name
   * @param args Output: argument strings
   * @return true if parsing succeeded
   */
  bool parseIntrinsic(const std::string &text, std::string &name,
                      std::vector<std::string> &args);

  /**
   * Generate code for an expression argument
   * @param arg The argument string (could be literal, variable, or nested
   * expression)
   * @param localVars Map of local variable names to their LLVM values
   * @return The generated LLVM value
   */
  llvm::Value *generateExpression(
      const std::string &arg,
      const std::unordered_map<std::string, llvm::Value *> &localVars);

  /**
   * Create an alloca instruction in the entry block
   */
  llvm::AllocaInst *createEntryAlloca(llvm::Function *function,
                                      const std::string &name,
                                      llvm::Type *type);

  /**
   * Extract an argument from text starting at pos
   * Handles @intrinsic(...), quoted strings, and regular words
   * @param text The full text to extract from
   * @param pos The position to start at (updated to end position)
   * @return The extracted argument string
   */
  std::string extractArgument(const std::string &text, size_t &pos);

  /**
   * Extract an argument from text until a specific literal word is found
   * @param text The full text to extract from
   * @param pos The position to start at (updated to end position)
   * @param untilWord The literal word to stop before
   * @return The extracted argument string
   */
  std::string extractArgumentUntil(const std::string &text, size_t &pos,
                                   const std::string &untilWord);

  // =========================================================================
  // LLVM Infrastructure
  // =========================================================================

  std::unique_ptr<llvm::LLVMContext> context;
  std::unique_ptr<llvm::Module> module;
  std::unique_ptr<llvm::IRBuilder<>> builder;

  // Printf declaration for print intrinsic
  llvm::FunctionCallee printfFunc;

  // Format strings for printf
  llvm::Value *fmtInt = nullptr;   // "%lld\n"
  llvm::Value *fmtFloat = nullptr; // "%f\n"
  llvm::Value *fmtStr = nullptr;   // "%s\n"

  // =========================================================================
  // Pattern Management
  // =========================================================================

  // Type inference instance (when running internally)
  std::unique_ptr<TypeInference> typeInference;

  // Codegen pattern data
  std::vector<std::unique_ptr<CodegenPattern>> codegenPatterns;

  // Map from ResolvedPattern to CodegenPattern
  std::unordered_map<ResolvedPattern *, CodegenPattern *> patternToCodegen;

  // Named values (variables) in current scope
  std::unordered_map<std::string, llvm::AllocaInst *> namedValues;

  // Current function being generated
  llvm::Function *currentFunction = nullptr;

  // Reference to the pattern resolver for expression matching
  SectionPatternResolver *resolverRef = nullptr;

  // Call stack for frame/section resolution
  struct FrameContext {
    CodegenPattern *pattern = nullptr;
    CodeLine *callSite = nullptr;
    std::unordered_map<std::string, llvm::Value *> locals;
  };
  std::vector<FrameContext> callStack;

  // =========================================================================
  // Error Handling
  // =========================================================================

  std::vector<Diagnostic> diagnosticsData;
};

} // namespace tbx
