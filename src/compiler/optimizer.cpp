#include "compiler/optimizer.hpp"

#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Verifier.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/Transforms/InstCombine/InstCombine.h>
#include <llvm/Transforms/Scalar.h>
#include <llvm/Transforms/Scalar/GVN.h>
#include <llvm/Transforms/Utils.h>

#include <cstdlib>

namespace tbx {

bool Optimizer::targetsInitialized = false;

Optimizer::Optimizer(OptimizationLevel levelParam) : level(levelParam) {
  initializeTargets();
}

Optimizer::~Optimizer() = default;

void Optimizer::setOptimizationLevel(OptimizationLevel levelParam) {
  level = levelParam;
}

void Optimizer::initializeTargets() {
  if (targetsInitialized) {
    return;
  }

  // Initialize native target
  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();
  llvm::InitializeNativeTargetAsmParser();

  targetsInitialized = true;
}

llvm::TargetMachine *Optimizer::getTargetMachine() {
  if (targetMachine) {
    return targetMachine.get();
  }

  // Get the target triple for the current machine
  std::string targetTriple = llvm::sys::getDefaultTargetTriple();

  // Look up the target
  std::string errorString;
  const llvm::Target *target =
      llvm::TargetRegistry::lookupTarget(targetTriple, errorString);
  if (!target) {
    errorsData.push_back("Could not find target: " + errorString);
    return nullptr;
  }

  // Create target machine with appropriate options
  llvm::TargetOptions options;

  // Set optimization level for code generation
  llvm::CodeGenOptLevel cgOptLevel;
  switch (level) {
  case OptimizationLevel::O0:
    cgOptLevel = llvm::CodeGenOptLevel::None;
    break;
  case OptimizationLevel::O1:
    cgOptLevel = llvm::CodeGenOptLevel::Less;
    break;
  case OptimizationLevel::O2:
    cgOptLevel = llvm::CodeGenOptLevel::Default;
    break;
  case OptimizationLevel::O3:
    cgOptLevel = llvm::CodeGenOptLevel::Aggressive;
    break;
  }

  targetMachine.reset(target->createTargetMachine(
      targetTriple,
      "generic", // CPU
      "",        // Features
      options, llvm::Reloc::PIC_, llvm::CodeModel::Small, cgOptLevel));

  if (!targetMachine) {
    errorsData.push_back("Could not create target machine");
    return nullptr;
  }

  return targetMachine.get();
}

bool Optimizer::optimize(llvm::Module &module) {
  // Verify the module before optimization
  std::string verifyError;
  llvm::raw_string_ostream verifyOs(verifyError);
  if (llvm::verifyModule(module, &verifyOs)) {
    errorsData.push_back("Module verification failed before optimization: " +
                         verifyError);
    return false;
  }

  // Get target machine for target-specific optimizations
  llvm::TargetMachine *tm = getTargetMachine();
  if (!tm) {
    return false;
  }

  // Set up the module with target information
  module.setTargetTriple(tm->getTargetTriple().str());
  module.setDataLayout(tm->createDataLayout());

  // Create the analysis managers
  llvm::LoopAnalysisManager lam;
  llvm::FunctionAnalysisManager fam;
  llvm::CGSCCAnalysisManager cgam;
  llvm::ModuleAnalysisManager mam;

  // Create the pass builder with default pipelines
  llvm::PassBuilder pb(tm);

  // Register all the basic analyses
  pb.registerModuleAnalyses(mam);
  pb.registerCGSCCAnalyses(cgam);
  pb.registerFunctionAnalyses(fam);
  pb.registerLoopAnalyses(lam);
  pb.crossRegisterProxies(lam, fam, cgam, mam);

  // Build the optimization pipeline based on optimization level
  llvm::ModulePassManager mpm;

  switch (level) {
  case OptimizationLevel::O0:
    // No optimizations, just run AlwaysInliner for functions marked
    // always_inline
    mpm = pb.buildO0DefaultPipeline(llvm::OptimizationLevel::O0);
    break;
  case OptimizationLevel::O1:
    mpm = pb.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O1);
    break;
  case OptimizationLevel::O2:
    mpm = pb.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O2);
    break;
  case OptimizationLevel::O3:
    mpm = pb.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O3);
    break;
  }

  // Run the optimizations
  mpm.run(module, mam);

  // Verify the module after optimization
  verifyError.clear();
  if (llvm::verifyModule(module, &verifyOs)) {
    errorsData.push_back("Module verification failed after optimization: " +
                         verifyError);
    return false;
  }

  return true;
}

bool Optimizer::emitToFile(llvm::Module &module, const std::string &outputPath,
                           llvm::CodeGenFileType fileType) {
  llvm::TargetMachine *tm = getTargetMachine();
  if (!tm) {
    return false;
  }

  // Set up module with target info if not already done
  if (module.getTargetTriple().empty()) {
    module.setTargetTriple(tm->getTargetTriple().str());
    module.setDataLayout(tm->createDataLayout());
  }

  // Open output file
  std::error_code ec;
  llvm::raw_fd_ostream out(outputPath, ec, llvm::sys::fs::OF_None);
  if (ec) {
    errorsData.push_back("Could not open output file '" + outputPath +
                         "': " + ec.message());
    return false;
  }

  // Use legacy pass manager for code generation (required by LLVM)
  llvm::legacy::PassManager passManager;

  if (tm->addPassesToEmitFile(passManager, out, nullptr, fileType)) {
    errorsData.push_back("Target machine cannot emit file of this type");
    return false;
  }

  passManager.run(module);
  out.flush();

  return true;
}

bool Optimizer::emitObjectFile(llvm::Module &module,
                               const std::string &outputPath) {
  return emitToFile(module, outputPath, llvm::CodeGenFileType::ObjectFile);
}

bool Optimizer::emitAssembly(llvm::Module &module,
                             const std::string &outputPath) {
  return emitToFile(module, outputPath, llvm::CodeGenFileType::AssemblyFile);
}

bool Optimizer::emitLlvmIr(llvm::Module &module,
                           const std::string &outputPath) {
  std::error_code ec;
  llvm::raw_fd_ostream out(outputPath, ec, llvm::sys::fs::OF_None);
  if (ec) {
    errorsData.push_back("Could not open output file '" + outputPath +
                         "': " + ec.message());
    return false;
  }

  module.print(out, nullptr);
  out.flush();

  return true;
}

bool Optimizer::emitExecutable(llvm::Module &module,
                               const std::string &outputPath) {
  // First emit an object file to a temporary location
  std::string objectPath = outputPath + ".o";

  if (!emitObjectFile(module, objectPath)) {
    return false;
  }

  // Link the object file into an executable using the system linker
  // Use clang or cc as the linker driver
  std::string linkCommand =
      "cc -o \"" + outputPath + "\" \"" + objectPath + "\" -lm";

  int result = std::system(linkCommand.c_str());
  if (result != 0) {
    errorsData.push_back("Linking failed with exit code " +
                         std::to_string(result));
    // Clean up the object file
    std::remove(objectPath.c_str());
    return false;
  }

  // Clean up the temporary object file
  std::remove(objectPath.c_str());

  return true;
}

OptimizationLevel Optimizer::parseOptimizationLevel(const std::string &str) {
  if (str == "0" || str == "O0" || str == "-O0") {
    return OptimizationLevel::O0;
  } else if (str == "1" || str == "O1" || str == "-O1") {
    return OptimizationLevel::O1;
  } else if (str == "2" || str == "O2" || str == "-O2") {
    return OptimizationLevel::O2;
  } else if (str == "3" || str == "O3" || str == "-O3") {
    return OptimizationLevel::O3;
  }
  // Default to O2
  return OptimizationLevel::O2;
}

} // namespace tbx
