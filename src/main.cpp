#include "compiler/codeGenerator.hpp"
#include "compiler/importResolver.hpp"
#include "compiler/optimizer.hpp"
#include "compiler/patternResolver.hpp"
#include "compiler/sectionAnalyzer.hpp"
#include "compiler/typeInference.hpp"
#include "dap/dapServer.hpp"
#include "lexer/lexer.hpp"
#include "lsp/lspServer.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

// LLVM JIT includes
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/TargetSelect.h>

namespace fs = std::filesystem;

void printUsage(const char *program) {
  std::cerr << "Usage: " << program << " [options] <source_file.3bx>\n";
  std::cerr << "       " << program << " --lsp [--debug]\n";
  std::cerr << "       " << program << " --dap [--debug]\n";
  std::cerr << "\nCompilation Options:\n";
  std::cerr << "  -o <file>       Write output to <file>\n";
  std::cerr << "  -O0             No optimization (for debugging)\n";
  std::cerr << "  -O1             Basic optimizations\n";
  std::cerr << "  -O2             Standard optimizations (default)\n";
  std::cerr << "  -O3             Aggressive optimizations\n";
  std::cerr << "  --emit-llvm     Output LLVM IR (.ll) instead of binary\n";
  std::cerr << "  --emit-asm      Output assembly (.s) instead of binary\n";
  std::cerr
      << "  --emit-obj      Output object file (.o) instead of executable\n";
  std::cerr << "  -c              Same as --emit-obj\n";
  std::cerr << "  -S              Same as --emit-asm\n";
  std::cerr << "\nDebug/Analysis Options:\n";
  std::cerr << "  --emit-ir       Output LLVM IR to stdout (legacy, use "
               "--emit-llvm)\n";
  std::cerr << "  --analyze       Run import resolution and section analysis "
               "(Steps 1-2)\n";
  std::cerr << "  --resolve       Run pattern resolution (Steps 1-3)\n";
  std::cerr << "  --typecheck     Run type inference (Steps 1-4)\n";
  std::cerr
      << "  --codegen       Run code generation (Steps 1-5) - output LLVM IR\n";
  std::cerr << "\nServer Modes:\n";
  std::cerr << "  --lsp           Start Language Server Protocol mode\n";
  std::cerr << "  --dap           Start Debug Adapter Protocol mode\n";
  std::cerr << "  --debug         Enable debug logging (with --lsp or --dap)\n";
}

std::string readFile(const std::string &path) {
  std::ifstream file(path);
  if (!file) {
    throw std::runtime_error("Cannot open file: " + path);
  }
  std::stringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

// Helper to derive output filename from source file
std::string deriveOutputPath(const std::string &sourceFile,
                             const std::string &extension) {
  fs::path sourcePath(sourceFile);
  fs::path outputPath = sourcePath.parent_path() / sourcePath.stem();
  return outputPath.string() + extension;
}

// Run the compiled module using LLVM ORC JIT
int runJit(std::unique_ptr<llvm::LLVMContext> context,
           std::unique_ptr<llvm::Module> module) {
  // Initialize native target
  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();
  llvm::InitializeNativeTargetAsmParser();

  // Create JIT
  auto jitExpected = llvm::orc::LLJITBuilder().create();
  if (!jitExpected) {
    std::cerr << "Error creating JIT: "
              << llvm::toString(jitExpected.takeError()) << "\n";
    return 1;
  }
  auto jit = std::move(*jitExpected);

  // Add the module to JIT
  auto tsm = llvm::orc::ThreadSafeModule(std::move(module), std::move(context));
  if (auto err = jit->addIRModule(std::move(tsm))) {
    std::cerr << "Error adding module to JIT: "
              << llvm::toString(std::move(err)) << "\n";
    return 1;
  }

  // Look up the main function
  auto mainSymbol = jit->lookup("main");
  if (!mainSymbol) {
    std::cerr << "Error looking up main: "
              << llvm::toString(mainSymbol.takeError()) << "\n";
    return 1;
  }

  // Get the function pointer and call it
  auto *mainFn = mainSymbol->toPtr<int()>();
  return mainFn();
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    printUsage(argv[0]);
    return 1;
  }

  bool emitIr = false;   // Legacy: output IR to stdout
  bool emitLlvm = false; // Output IR to file
  bool emitAsm = false;  // Output assembly to file
  bool emitObj = false;  // Output object file
  bool lspMode = false;
  bool dapMode = false;
  bool debugMode = false;
  bool analyzeMode = false;
  bool resolveMode = false;
  bool typecheckMode = false;
  bool codegenMode = false;
  std::string sourceFile;
  std::string outputFile;
  tbx::OptimizationLevel optimizationLevel = tbx::OptimizationLevel::O2;

  for (int argIndex = 1; argIndex < argc; argIndex++) {
    std::string arg = argv[argIndex];
    if (arg == "--emit-ir") {
      emitIr = true;
    } else if (arg == "--emit-llvm") {
      emitLlvm = true;
    } else if (arg == "--emit-asm" || arg == "-S") {
      emitAsm = true;
    } else if (arg == "--emit-obj" || arg == "-c") {
      emitObj = true;
    } else if (arg == "-o" && argIndex + 1 < argc) {
      outputFile = argv[++argIndex];
    } else if (arg == "-O0") {
      optimizationLevel = tbx::OptimizationLevel::O0;
    } else if (arg == "-O1") {
      optimizationLevel = tbx::OptimizationLevel::O1;
    } else if (arg == "-O2") {
      optimizationLevel = tbx::OptimizationLevel::O2;
    } else if (arg == "-O3") {
      optimizationLevel = tbx::OptimizationLevel::O3;
    } else if (arg == "--lsp") {
      lspMode = true;
    } else if (arg == "--dap") {
      dapMode = true;
    } else if (arg == "--debug") {
      debugMode = true;
    } else if (arg == "--analyze") {
      analyzeMode = true;
    } else if (arg == "--resolve") {
      resolveMode = true;
    } else if (arg == "--typecheck") {
      typecheckMode = true;
    } else if (arg == "--codegen") {
      codegenMode = true;
    } else if (arg == "--help" || arg == "-h") {
      printUsage(argv[0]);
      return 0;
    } else if (arg[0] != '-') {
      sourceFile = arg;
    } else {
      std::cerr << "Unknown option: " << arg << "\n";
      printUsage(argv[0]);
      return 1;
    }
  }

  // Handle LSP mode
  if (lspMode) {
    tbx::LspServer server;
    server.setDebug(debugMode);
    server.run();
    return 0;
  }

  // Handle DAP mode
  if (dapMode) {
    tbx::DapServer server;
    server.setDebug(debugMode);
    server.run();
    return 0;
  }

  if (sourceFile.empty()) {
    std::cerr << "Error: No source file specified\n";
    return 1;
  }

  try {
    // Handle analyze mode (new pipeline: Steps 1-2)
    if (analyzeMode) {
      // Step 1: Import Resolution
      std::cout << "=== Step 1: Import Resolution ===\n\n";
      fs::path sourcePathAbs = fs::absolute(sourceFile);
      tbx::ImportResolver resolver(sourcePathAbs.parent_path().string());
      std::string mergedSource =
          resolver.resolveWithPrelude(sourcePathAbs.string());

      if (!resolver.diagnostics().empty()) {
        for (const auto &diagnostic : resolver.diagnostics()) {
          std::cerr << diagnostic.toString() << "\n";
        }
      }

      std::cout << "Resolved files:\n";
      for (const auto &file : resolver.resolvedFiles()) {
        std::cout << "  - " << file << "\n";
      }
      std::cout << "\n";

      // Step 2: Section Analysis
      std::cout << "=== Step 2: Section Analysis ===\n\n";
      tbx::SectionAnalyzer analyzer;
      // Convert SourceMap type
      std::map<int, tbx::SectionAnalyzer::SourceLocation> sectionSourceMap;
      for (const auto &[line, loc] : resolver.sourceMap()) {
        sectionSourceMap[line] = {loc.filePath, loc.lineNumber};
      }
      auto rootSection = analyzer.analyze(mergedSource, sectionSourceMap);

      if (!analyzer.diagnostics().empty()) {
        for (const auto &diagnostic : analyzer.diagnostics()) {
          std::cerr << diagnostic.toString() << "\n";
        }
      }

      std::cout << "Section Tree:\n";
      rootSection->print(0);

      return 0;
    }

    // Handle resolve mode (new pipeline: Steps 1-3)
    if (resolveMode) {
      // Step 1: Import Resolution
      std::cout << "=== Step 1: Import Resolution ===\n\n";
      fs::path sourcePathAbs = fs::absolute(sourceFile);
      tbx::ImportResolver importResolver(sourcePathAbs.parent_path().string());
      std::string mergedSource =
          importResolver.resolveWithPrelude(sourcePathAbs.string());

      if (!importResolver.diagnostics().empty()) {
        for (const auto &diagnostic : importResolver.diagnostics()) {
          std::cerr << diagnostic.toString() << "\n";
        }
      }

      std::cout << "Resolved files:\n";
      for (const auto &file : importResolver.resolvedFiles()) {
        std::cout << "  - " << file << "\n";
      }
      std::cout << "\n";

      // Step 2: Section Analysis
      std::cout << "=== Step 2: Section Analysis ===\n\n";
      tbx::SectionAnalyzer sectionAnalyzer;
      // Convert SourceMap type
      std::map<int, tbx::SectionAnalyzer::SourceLocation> sectionSourceMap;
      for (const auto &[line, loc] : importResolver.sourceMap()) {
        sectionSourceMap[line] = {loc.filePath, loc.lineNumber};
      }
      auto rootSection =
          sectionAnalyzer.analyze(mergedSource, sectionSourceMap);

      if (!sectionAnalyzer.diagnostics().empty()) {
        for (const auto &diagnostic : sectionAnalyzer.diagnostics()) {
          std::cerr << diagnostic.toString() << "\n";
        }
      }

      std::cout << "Section Tree:\n";
      rootSection->print(0);
      std::cout << "\n";

      // Step 3: Pattern Resolution
      std::cout << "=== Step 3: Pattern Resolution ===\n\n";
      tbx::SectionPatternResolver patternResolver;
      bool resolved = patternResolver.resolve(rootSection.get());

      if (!patternResolver.diagnostics().empty()) {
        for (const auto &diagnostic : patternResolver.diagnostics()) {
          std::cerr << diagnostic.toString() << "\n";
        }
      }

      patternResolver.printResults();

      if (resolved) {
        std::cout << "\nAll patterns resolved successfully.\n";
      } else {
        std::cout << "\nSome patterns could not be resolved.\n";
      }

      return resolved ? 0 : 1;
    }

    // Handle typecheck mode (new pipeline: Steps 1-4)
    if (typecheckMode) {
      // Step 1: Import Resolution
      std::cout << "=== Step 1: Import Resolution ===\n\n";
      fs::path sourcePathAbs = fs::absolute(sourceFile);
      tbx::ImportResolver importResolver(sourcePathAbs.parent_path().string());
      std::string mergedSource =
          importResolver.resolveWithPrelude(sourcePathAbs.string());

      if (!importResolver.diagnostics().empty()) {
        for (const auto &diagnostic : importResolver.diagnostics()) {
          std::cerr << diagnostic.toString() << "\n";
        }
      }

      std::cout << "Resolved files:\n";
      for (const auto &file : importResolver.resolvedFiles()) {
        std::cout << "  - " << file << "\n";
      }
      std::cout << "\n";

      // Step 2: Section Analysis
      std::cout << "=== Step 2: Section Analysis ===\n\n";
      tbx::SectionAnalyzer sectionAnalyzer;
      // Convert SourceMap type
      std::map<int, tbx::SectionAnalyzer::SourceLocation> sectionSourceMap;
      for (const auto &[line, loc] : importResolver.sourceMap()) {
        sectionSourceMap[line] = {loc.filePath, loc.lineNumber};
      }
      auto rootSection =
          sectionAnalyzer.analyze(mergedSource, sectionSourceMap);

      if (!sectionAnalyzer.diagnostics().empty()) {
        for (const auto &diagnostic : sectionAnalyzer.diagnostics()) {
          std::cerr << diagnostic.toString() << "\n";
        }
      }

      std::cout << "Section Tree:\n";
      rootSection->print(0);
      std::cout << "\n";

      // Step 3: Pattern Resolution
      std::cout << "=== Step 3: Pattern Resolution ===\n\n";
      tbx::SectionPatternResolver patternResolver;
      bool resolved = patternResolver.resolve(rootSection.get());

      if (!patternResolver.diagnostics().empty()) {
        for (const auto &diagnostic : patternResolver.diagnostics()) {
          std::cerr << diagnostic.toString() << "\n";
        }
      }

      patternResolver.printResults();
      std::cout << "\n";

      // Step 4: Type Inference
      std::cout << "=== Step 4: Type Inference ===\n\n";
      tbx::TypeInference typeInference;
      bool typed = typeInference.infer(patternResolver);

      if (!typeInference.diagnostics().empty()) {
        for (const auto &diagnostic : typeInference.diagnostics()) {
          std::cerr << diagnostic.toString() << "\n";
        }
      }

      typeInference.printResults();

      if (resolved && typed) {
        std::cout << "\nAll patterns resolved and typed successfully.\n";
      } else if (!resolved) {
        std::cout << "\nSome patterns could not be resolved.\n";
      } else {
        std::cout << "\nSome types could not be inferred.\n";
      }

      return (resolved && typed) ? 0 : 1;
    }

    // Handle codegen mode (new pipeline: Steps 1-5)
    if (codegenMode) {
      // Step 1: Import Resolution
      std::cout << "=== Step 1: Import Resolution ===\n\n";
      fs::path sourcePathAbs = fs::absolute(sourceFile);
      tbx::ImportResolver importResolver(sourcePathAbs.parent_path().string());
      std::string mergedSource =
          importResolver.resolveWithPrelude(sourcePathAbs.string());

      if (!importResolver.diagnostics().empty()) {
        for (const auto &diagnostic : importResolver.diagnostics()) {
          std::cerr << diagnostic.toString() << "\n";
        }
      }

      std::cout << "Resolved files:\n";
      for (const auto &file : importResolver.resolvedFiles()) {
        std::cout << "  - " << file << "\n";
      }
      std::cout << "\n";

      // Step 2: Section Analysis
      std::cout << "=== Step 2: Section Analysis ===\n\n";
      tbx::SectionAnalyzer sectionAnalyzer;
      // Convert SourceMap type
      std::map<int, tbx::SectionAnalyzer::SourceLocation> sectionSourceMap;
      for (const auto &[line, loc] : importResolver.sourceMap()) {
        sectionSourceMap[line] = {loc.filePath, loc.lineNumber};
      }
      auto rootSection =
          sectionAnalyzer.analyze(mergedSource, sectionSourceMap);

      if (!sectionAnalyzer.diagnostics().empty()) {
        for (const auto &diagnostic : sectionAnalyzer.diagnostics()) {
          std::cerr << diagnostic.toString() << "\n";
        }
      }

      std::cout << "Section Tree:\n";
      rootSection->print(0);
      std::cout << "\n";

      // Step 3: Pattern Resolution
      std::cout << "=== Step 3: Pattern Resolution ===\n\n";
      tbx::SectionPatternResolver patternResolver;
      bool resolved = patternResolver.resolve(rootSection.get());

      if (!patternResolver.diagnostics().empty()) {
        for (const auto &diagnostic : patternResolver.diagnostics()) {
          std::cerr << diagnostic.toString() << "\n";
        }
      }

      patternResolver.printResults();

      if (!resolved) {
        std::cout << "\nSome patterns could not be resolved.\n";
        return 1;
      }
      std::cout << "\nAll patterns resolved successfully.\n\n";

      // Step 4 & 5: Type Inference and Code Generation
      std::cout << "=== Steps 4-5: Type Inference and Code Generation ===\n\n";
      tbx::SectionCodeGenerator codeGenerator(sourceFile);
      bool generated =
          codeGenerator.generate(patternResolver, rootSection.get());

      if (!codeGenerator.diagnostics().empty()) {
        for (const auto &diagnostic : codeGenerator.diagnostics()) {
          std::cerr << diagnostic.toString() << "\n";
        }
      }

      if (!generated) {
        std::cout << "Code generation failed.\n";
        return 1;
      }

      std::cout << "Generated LLVM IR:\n\n";
      codeGenerator.printIr();

      return 0;
    }

    // =========================================================================
    // Default compilation path - New Pipeline
    // =========================================================================

    // Step 1: Import Resolution
    fs::path sourcePathAbs = fs::absolute(sourceFile);
    tbx::ImportResolver importResolver(sourcePathAbs.parent_path().string());
    std::string mergedSource =
        importResolver.resolveWithPrelude(sourcePathAbs.string());

    if (!importResolver.diagnostics().empty()) {
      for (const auto &diagnostic : importResolver.diagnostics()) {
        std::cerr << diagnostic.toString() << "\n";
      }
      return 1;
    }

    // Step 2: Section Analysis
    tbx::SectionAnalyzer sectionAnalyzer;
    // Convert SourceMap type
    std::map<int, tbx::SectionAnalyzer::SourceLocation> sectionSourceMap;
    for (const auto &[line, loc] : importResolver.sourceMap()) {
      sectionSourceMap[line] = {loc.filePath, loc.lineNumber};
    }
    auto rootSection = sectionAnalyzer.analyze(mergedSource, sectionSourceMap);

    if (!sectionAnalyzer.diagnostics().empty()) {
      for (const auto &diagnostic : sectionAnalyzer.diagnostics()) {
        std::cerr << diagnostic.toString() << "\n";
      }
      return 1;
    }

    // Step 3: Pattern Resolution
    tbx::SectionPatternResolver patternResolver;
    bool resolved = patternResolver.resolve(rootSection.get());

    if (!patternResolver.diagnostics().empty()) {
      for (const auto &diagnostic : patternResolver.diagnostics()) {
        std::cerr << diagnostic.toString() << "\n";
      }
    }

    if (!resolved) {
      std::cerr << "Error: Some patterns could not be resolved.\n";
      return 1;
    }

    // Steps 4-5: Type Inference and Code Generation
    tbx::SectionCodeGenerator codeGenerator(sourceFile);
    bool generated = codeGenerator.generate(patternResolver, rootSection.get());

    if (!codeGenerator.diagnostics().empty()) {
      for (const auto &diagnostic : codeGenerator.diagnostics()) {
        std::cerr << diagnostic.toString() << "\n";
      }
    }

    if (!generated) {
      std::cerr << "Error: Code generation failed.\n";
      return 1;
    }

    // Legacy: emit IR to stdout (unoptimized)
    if (emitIr) {
      codeGenerator.printIr();
      return 0;
    }

    // Get the module for optimization/output
    llvm::Module *module = codeGenerator.getModule();

    // Step 6: Optimization and Output
    tbx::Optimizer optimizer(optimizationLevel);

    // Apply optimizations
    if (!optimizer.optimize(*module)) {
      for (const auto &err : optimizer.errors()) {
        std::cerr << "Optimization Error: " << err << "\n";
      }
      return 1;
    }

    // Handle file output modes
    if (emitLlvm || emitAsm || emitObj) {
      // Determine output file path
      std::string outPath = outputFile;

      if (emitLlvm) {
        if (outPath.empty()) {
          outPath = deriveOutputPath(sourceFile, ".ll");
        }
        if (!optimizer.emitLlvmIr(*module, outPath)) {
          for (const auto &err : optimizer.errors()) {
            std::cerr << "Error: " << err << "\n";
          }
          return 1;
        }
        std::cout << "Wrote LLVM IR to " << outPath << "\n";
      } else if (emitAsm) {
        if (outPath.empty()) {
          outPath = deriveOutputPath(sourceFile, ".s");
        }
        if (!optimizer.emitAssembly(*module, outPath)) {
          for (const auto &err : optimizer.errors()) {
            std::cerr << "Error: " << err << "\n";
          }
          return 1;
        }
        std::cout << "Wrote assembly to " << outPath << "\n";
      } else if (emitObj) {
        if (outPath.empty()) {
          outPath = deriveOutputPath(sourceFile, ".o");
        }
        if (!optimizer.emitObjectFile(*module, outPath)) {
          for (const auto &err : optimizer.errors()) {
            std::cerr << "Error: " << err << "\n";
          }
          return 1;
        }
        std::cout << "Wrote object file to " << outPath << "\n";
      }
      return 0;
    }

    // If -o is specified without emit flags, generate an executable
    if (!outputFile.empty()) {
      if (!optimizer.emitExecutable(*module, outputFile)) {
        for (const auto &err : optimizer.errors()) {
          std::cerr << "Error: " << err << "\n";
        }
        return 1;
      }
      std::cout << "Wrote executable to " << outputFile << "\n";
      return 0;
    }

    // Default: run the compiled code using JIT
    auto context = codeGenerator.takeContext();
    auto jitModule = codeGenerator.takeModule();
    return runJit(std::move(context), std::move(jitModule));

  } catch (const std::exception &exception) {
    std::cerr << "Error: " << exception.what() << "\n";
    return 1;
  }
}
