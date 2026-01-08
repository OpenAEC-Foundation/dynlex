#pragma once

#include "compiler/optimizationLevel.hpp"
#include "compiler/outputFormat.hpp"

#include <llvm/IR/Module.h>
#include <llvm/Target/TargetMachine.h>

#include <memory>
#include <string>
#include <vector>

namespace tbx {

/**
 * Optimizer - Step 6 of the 3BX compiler pipeline
 *
 * Applies LLVM optimization passes and generates final output.
 *
 * Optimization passes include:
 * 1. Inlining: Small functions are inlined at call sites
 * 2. Constant folding: 5 + 3 becomes 8 at compile time
 * 3. Dead code elimination: Unused code is removed
 * 4. Register allocation: Variables are assigned to CPU registers
 */
class Optimizer {
public:
  /**
   * Construct an Optimizer with the specified optimization level
   * @param level The optimization level (default: O2)
   */
  explicit Optimizer(OptimizationLevel level = OptimizationLevel::O2);

  ~Optimizer();

  /**
   * Set the optimization level
   * @param level The optimization level
   */
  void setOptimizationLevel(OptimizationLevel level);

  /**
   * Get the current optimization level
   */
  OptimizationLevel optimizationLevel() const { return level; }

  /**
   * Apply optimization passes to a module
   * @param module The LLVM module to optimize
   * @return true if optimization succeeded, false otherwise
   */
  bool optimize(llvm::Module &module);

  /**
   * Emit object file from the module
   * @param module The LLVM module to compile
   * @param outputPath Path for the output object file
   * @return true if emission succeeded, false otherwise
   */
  bool emitObjectFile(llvm::Module &module, const std::string &outputPath);

  /**
   * Emit native executable from the module
   * @param module The LLVM module to compile
   * @param outputPath Path for the output executable
   * @return true if emission succeeded, false otherwise
   */
  bool emitExecutable(llvm::Module &module, const std::string &outputPath);

  /**
   * Emit LLVM IR (.ll file) from the module
   * @param module The LLVM module to output
   * @param outputPath Path for the output .ll file
   * @return true if emission succeeded, false otherwise
   */
  bool emitLlvmIr(llvm::Module &module, const std::string &outputPath);

  /**
   * Emit assembly (.s file) from the module
   * @param module The LLVM module to compile
   * @param outputPath Path for the output .s file
   * @return true if emission succeeded, false otherwise
   */
  bool emitAssembly(llvm::Module &module, const std::string &outputPath);

  /**
   * Get any errors that occurred during optimization/emission
   */
  const std::vector<std::string> &errors() const { return errorsData; }

  /**
   * Clear accumulated errors
   */
  void clearErrors() { errorsData.clear(); }

  /**
   * Convert optimization level string to enum
   * @param str String like "0", "1", "2", "3" or "O0", "O1", "O2", "O3"
   * @return The corresponding OptimizationLevel
   */
  static OptimizationLevel parseOptimizationLevel(const std::string &str);

private:
  /**
   * Initialize the LLVM target machinery (called once)
   */
  static void initializeTargets();

  /**
   * Get or create the target machine for the current platform
   */
  llvm::TargetMachine *getTargetMachine();

  /**
   * Emit code to a file using the target machine
   * @param module The module to emit
   * @param outputPath Output file path
   * @param fileType LLVM code generation file type
   * @return true if successful
   */
  bool emitToFile(llvm::Module &module, const std::string &outputPath,
                  llvm::CodeGenFileType fileType);

  OptimizationLevel level;
  std::unique_ptr<llvm::TargetMachine> targetMachine;
  std::vector<std::string> errorsData;

  static bool targetsInitialized;
};

} // namespace tbx
