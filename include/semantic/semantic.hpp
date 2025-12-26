#pragma once

#include "ast/ast.hpp"
#include <unordered_map>
#include <string>
#include <vector>

namespace tbx {

// Type representation
enum class Type {
    VOID,
    INTEGER,
    FLOAT,
    STRING,
    BOOLEAN,
    FUNCTION,
    UNKNOWN
};

std::string type_to_string(Type type);

// Symbol table entry
struct Symbol {
    std::string name;
    Type type;
    bool is_mutable;
};

// Semantic analyzer
class SemanticAnalyzer : public ASTVisitor {
public:
    SemanticAnalyzer();

    // Analyze program and report errors
    bool analyze(Program& program);

    // Get collected errors
    const std::vector<std::string>& errors() const { return errors_; }

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
    std::vector<std::unordered_map<std::string, Symbol>> scopes_;
    std::vector<std::string> errors_;
    Type last_type_ = Type::UNKNOWN;

    void push_scope();
    void pop_scope();
    void define(const std::string& name, Type type, bool mutable_ = true);
    Symbol* lookup(const std::string& name);
    void error(const std::string& message);
};

} // namespace tbx
