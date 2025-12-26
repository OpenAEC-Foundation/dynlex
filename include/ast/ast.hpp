#pragma once

#include "lexer/token.hpp"
#include <memory>
#include <vector>
#include <string>
#include <unordered_map>
#include <variant>

namespace tbx {

// Forward declarations
struct ASTVisitor;

// Base AST node
struct ASTNode {
    SourceLocation location;
    virtual ~ASTNode() = default;
    virtual void accept(ASTVisitor& visitor) = 0;
};

using ASTNodePtr = std::unique_ptr<ASTNode>;

// Expression nodes
struct Expression : ASTNode {};
using ExprPtr = std::unique_ptr<Expression>;

struct IntegerLiteral : Expression {
    int64_t value;
    void accept(ASTVisitor& visitor) override;
};

struct FloatLiteral : Expression {
    double value;
    void accept(ASTVisitor& visitor) override;
};

struct StringLiteral : Expression {
    std::string value;
    void accept(ASTVisitor& visitor) override;
};

struct Identifier : Expression {
    std::string name;
    void accept(ASTVisitor& visitor) override;
};

struct BinaryExpr : Expression {
    ExprPtr left;
    TokenType op;
    ExprPtr right;
    void accept(ASTVisitor& visitor) override;
};

// Natural language expression - a sequence of tokens that form a pattern call
// Used when the parser encounters natural language syntax like "a new vector"
struct NaturalExpr : Expression {
    std::vector<Token> tokens;           // The raw tokens
    void accept(ASTVisitor& visitor) override;
};

// Statement nodes
struct Statement : ASTNode {};
using StmtPtr = std::unique_ptr<Statement>;

struct ExpressionStmt : Statement {
    ExprPtr expression;
    void accept(ASTVisitor& visitor) override;
};

struct SetStatement : Statement {
    std::string variable;
    ExprPtr value;
    void accept(ASTVisitor& visitor) override;
};

struct IfStatement : Statement {
    ExprPtr condition;
    std::vector<StmtPtr> then_branch;
    std::vector<StmtPtr> else_branch;
    void accept(ASTVisitor& visitor) override;
};

struct WhileStatement : Statement {
    ExprPtr condition;
    std::vector<StmtPtr> body;
    void accept(ASTVisitor& visitor) override;
};

struct FunctionDecl : Statement {
    std::string name;
    std::vector<std::string> params;
    std::vector<StmtPtr> body;
    void accept(ASTVisitor& visitor) override;
};

// ============================================
// Pattern System AST Nodes
// ============================================

// Intrinsic call: @intrinsic("name", arg1, arg2, ...)
struct IntrinsicCall : Expression {
    std::string name;                    // Intrinsic name (e.g., "store", "add")
    std::vector<ExprPtr> args;           // Arguments
    void accept(ASTVisitor& visitor) override;
};

// A single element in a pattern syntax (either literal word or parameter)
struct PatternElement {
    bool is_param;           // true if parameter, false if literal word
    bool is_optional;        // true if element is optional (e.g., [the], [a])
    std::string value;       // Word text or parameter name
};

// Pattern definition
struct PatternDef : Statement {
    std::vector<PatternElement> syntax;  // The pattern syntax template
    std::vector<StmtPtr> when_parsed;    // Compile-time expansion (optional)
    std::vector<StmtPtr> when_triggered; // Runtime behavior
    void accept(ASTVisitor& visitor) override;
};

// Pattern invocation (matched pattern with captured arguments)
struct PatternCall : Expression {
    PatternDef* pattern;                           // Which pattern was matched
    std::unordered_map<std::string, ExprPtr> bindings; // param name -> captured expr
    void accept(ASTVisitor& visitor) override;
};

// Import statement
struct ImportStmt : Statement {
    std::string module_path;
    void accept(ASTVisitor& visitor) override;
};

// Use statement: use thing from module.3bx
struct UseStmt : Statement {
    std::string item_name;       // What to import (e.g., "vector")
    std::string module_path;     // Where to import from (e.g., "vector.3bx")
    void accept(ASTVisitor& visitor) override;
};

// Import function declaration for FFI: import function name(params) from "header"
struct ImportFunctionDecl : Statement {
    std::string name;                    // Function name (e.g., "glClearColor")
    std::vector<std::string> params;     // Parameter names (e.g., ["r", "g", "b", "a"])
    std::string header;                  // Header file path (e.g., "GL/gl.h")
    void accept(ASTVisitor& visitor) override;
};

// Program root
struct Program : ASTNode {
    std::vector<StmtPtr> statements;
    void accept(ASTVisitor& visitor) override;
};

// Visitor interface
struct ASTVisitor {
    virtual ~ASTVisitor() = default;
    virtual void visit(IntegerLiteral& node) = 0;
    virtual void visit(FloatLiteral& node) = 0;
    virtual void visit(StringLiteral& node) = 0;
    virtual void visit(Identifier& node) = 0;
    virtual void visit(BinaryExpr& node) = 0;
    virtual void visit(NaturalExpr& node) = 0;
    virtual void visit(ExpressionStmt& node) = 0;
    virtual void visit(SetStatement& node) = 0;
    virtual void visit(IfStatement& node) = 0;
    virtual void visit(WhileStatement& node) = 0;
    virtual void visit(FunctionDecl& node) = 0;
    virtual void visit(IntrinsicCall& node) = 0;
    virtual void visit(PatternDef& node) = 0;
    virtual void visit(PatternCall& node) = 0;
    virtual void visit(ImportStmt& node) = 0;
    virtual void visit(UseStmt& node) = 0;
    virtual void visit(ImportFunctionDecl& node) = 0;
    virtual void visit(Program& node) = 0;
};

} // namespace tbx
