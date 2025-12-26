#include "lexer/lexer.hpp"
#include "parser/parser.hpp"
#include "semantic/semantic.hpp"
#include "codegen/codegen.hpp"
#include "lsp/lspServer.hpp"

#include <iostream>
#include <fstream>
#include <sstream>
#include <set>
#include <filesystem>

namespace fs = std::filesystem;

void printUsage(const char* program) {
    std::cerr << "Usage: " << program << " <source_file.3bx>\n";
    std::cerr << "       " << program << " --emit-ir <source_file.3bx>\n";
    std::cerr << "       " << program << " --lsp [--debug]\n";
    std::cerr << "\nOptions:\n";
    std::cerr << "  --emit-ir    Output LLVM IR instead of compiling\n";
    std::cerr << "  --lsp        Start Language Server Protocol mode\n";
    std::cerr << "  --debug      Enable debug logging (with --lsp)\n";
}

std::string readFile(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("Cannot open file: " + path);
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// Get the directory where the compiler executable is located
std::string getExecutableDir() {
    return fs::canonical("/proc/self/exe").parent_path().string();
}

// Resolve import path relative to source file or lib directory
std::string resolveImport(const std::string& importPath, const std::string& sourceFile) {
    fs::path sourceDir = fs::path(sourceFile).parent_path();
    if (sourceDir.empty()) sourceDir = ".";

    // Try relative to source file first
    fs::path relativePath = sourceDir / importPath;
    if (fs::exists(relativePath)) {
        return relativePath.string();
    }

    // Try lib directory relative to source
    fs::path libPath = sourceDir / "lib" / importPath;
    if (fs::exists(libPath)) {
        return libPath.string();
    }

    // Search up the directory tree for lib folder (up to 5 levels)
    fs::path searchDir = sourceDir;
    for (int i = 0; i < 5; i++) {
        libPath = searchDir / "lib" / importPath;
        if (fs::exists(libPath)) {
            return fs::canonical(libPath).string();
        }
        fs::path parent = searchDir.parent_path();
        if (parent == searchDir) break; // Reached root
        searchDir = parent;
    }

    // Try lib directory relative to executable (for installed compiler)
    std::string exeDir = getExecutableDir();
    libPath = fs::path(exeDir) / ".." / "lib" / importPath;
    if (fs::exists(libPath)) {
        return fs::canonical(libPath).string();
    }

    // Try lib directory next to executable
    libPath = fs::path(exeDir) / "lib" / importPath;
    if (fs::exists(libPath)) {
        return libPath.string();
    }

    // Return original path, let it fail later with proper error
    return importPath;
}

// Parse a file and collect its imports recursively
// Uses shared registry to accumulate patterns across files
void parseWithImports(
    const std::string& filePath,
    std::set<std::string>& parsedFiles,
    std::vector<tbx::StmtPtr>& allStatements,
    tbx::PatternRegistry& sharedRegistry,
    const std::string& originalSource
) {
    // Avoid parsing the same file twice (circular import prevention)
    fs::path canonical = fs::weakly_canonical(filePath);
    std::string canonicalPath = canonical.string();

    if (parsedFiles.count(canonicalPath)) {
        return;
    }
    parsedFiles.insert(canonicalPath);

    // Read and parse the file
    std::string source = readFile(filePath);
    tbx::Lexer lexer(source, filePath);
    tbx::Parser parser(lexer);

    // Use shared registry so patterns from imports are available
    parser.setSharedRegistry(&sharedRegistry);

    auto program = parser.parse();

    // Process imports first (depth-first)
    // This ensures patterns from imports are registered before they're used
    for (auto& stmt : program->statements) {
        if (auto* importStmt = dynamic_cast<tbx::ImportStmt*>(stmt.get())) {
            std::string resolved = resolveImport(importStmt->module_path, filePath);
            parseWithImports(resolved, parsedFiles, allStatements, sharedRegistry, originalSource);
        } else if (auto* useStmt = dynamic_cast<tbx::UseStmt*>(stmt.get())) {
            std::string resolved = resolveImport(useStmt->module_path, filePath);
            parseWithImports(resolved, parsedFiles, allStatements, sharedRegistry, originalSource);
        }
    }

    // Add non-import statements to the collection
    for (auto& stmt : program->statements) {
        if (!dynamic_cast<tbx::ImportStmt*>(stmt.get()) &&
            !dynamic_cast<tbx::UseStmt*>(stmt.get())) {
            allStatements.push_back(std::move(stmt));
        }
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    bool emitIr = false;
    bool lspMode = false;
    bool debugMode = false;
    std::string sourceFile;

    for (int argIndex = 1; argIndex < argc; argIndex++) {
        std::string arg = argv[argIndex];
        if (arg == "--emit-ir") {
            emitIr = true;
        } else if (arg == "--lsp") {
            lspMode = true;
        } else if (arg == "--debug") {
            debugMode = true;
        } else if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        } else {
            sourceFile = arg;
        }
    }

    // Handle LSP mode
    if (lspMode) {
        tbx::LspServer server;
        server.setDebug(debugMode);
        server.run();
        return 0;
    }

    if (sourceFile.empty()) {
        std::cerr << "Error: No source file specified\n";
        return 1;
    }

    try {
        // Create shared pattern registry for all files
        tbx::PatternRegistry sharedRegistry;

        // Parse main file and all imports recursively
        std::set<std::string> parsedFiles;
        std::vector<tbx::StmtPtr> allStatements;
        parseWithImports(sourceFile, parsedFiles, allStatements, sharedRegistry, sourceFile);

        // Create combined program
        auto program = std::make_unique<tbx::Program>();
        program->statements = std::move(allStatements);

        // Semantic analysis
        tbx::SemanticAnalyzer analyzer;
        if (!analyzer.analyze(*program)) {
            for (const auto& err : analyzer.errors()) {
                std::cerr << "Error: " << err << "\n";
            }
            return 1;
        }

        // Code generation
        tbx::CodeGenerator codegen(sourceFile);
        if (!codegen.generate(*program)) {
            std::cerr << "Error: Code generation failed\n";
            return 1;
        }

        if (emitIr) {
            codegen.get_module()->print(llvm::outs(), nullptr);
        }

        std::cerr << "Compilation successful.\n";
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
