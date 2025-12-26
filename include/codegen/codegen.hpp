#pragma once

#include "ast/ast.hpp"

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Value.h>

#include <memory>
#include <set>
#include <string>
#include <unordered_map>

namespace tbx {

class CodeGenerator : public ASTVisitor {
public:
    CodeGenerator(const std::string& module_name);

    // Generate LLVM IR for program
    bool generate(Program& program);

    // Get the generated module
    llvm::Module* get_module() { return module_.get(); }

    // Get libraries used by the program (for linking)
    const std::set<std::string>& getUsedLibraries() const { return usedLibraries_; }

    // Output LLVM IR to file
    bool write_ir(const std::string& filename);

    // Compile to object file
    bool compile(const std::string& filename);

    // Visitor methods
    void visit(IntegerLiteral& node) override;
    void visit(FloatLiteral& node) override;
    void visit(StringLiteral& node) override;
    void visit(Identifier& node) override;
    void visit(BinaryExpr& node) override;
    void visit(NaturalExpr& node) override;
    void visit(ExpressionStmt& node) override;
    void visit(SetStatement& node) override;
    void visit(IfStatement& node) override;
    void visit(WhileStatement& node) override;
    void visit(FunctionDecl& node) override;
    void visit(IntrinsicCall& node) override;
    void visit(PatternDef& node) override;
    void visit(PatternCall& node) override;
    void visit(ImportStmt& node) override;
    void visit(UseStmt& node) override;
    void visit(ImportFunctionDecl& node) override;
    void visit(Program& node) override;

private:
    std::unique_ptr<llvm::LLVMContext> context_;
    std::unique_ptr<llvm::Module> module_;
    std::unique_ptr<llvm::IRBuilder<>> builder_;

    // Symbol table for variables
    std::unordered_map<std::string, llvm::AllocaInst*> named_values_;

    // Current value being built
    llvm::Value* current_value_ = nullptr;

    // Registry for imported external functions (FFI)
    struct ImportedFunction {
        std::string name;                    // Function name
        std::vector<std::string> params;     // Parameter names
        std::string header;                  // Header file
        llvm::Function* llvmFunc = nullptr;  // LLVM function declaration
    };
    std::unordered_map<std::string, ImportedFunction> importedFunctions_;

    // Set of libraries used (for linking)
    std::set<std::string> usedLibraries_;

    // Helper methods
    llvm::AllocaInst* create_entry_block_alloca(
        llvm::Function* function,
        const std::string& name,
        llvm::Type* type
    );

    // Create LLVM function declaration for an imported function
    llvm::Function* declareExternalFunction(const std::string& name, size_t paramCount);
};

} // namespace tbx
