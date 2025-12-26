#include "ast/ast.hpp"

namespace tbx {

void IntegerLiteral::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

void FloatLiteral::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

void StringLiteral::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

void Identifier::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

void BinaryExpr::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

void NaturalExpr::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

void ExpressionStmt::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

void SetStatement::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

void IfStatement::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

void WhileStatement::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

void FunctionDecl::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

void IntrinsicCall::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

void PatternDef::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

void PatternCall::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

void ImportStmt::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

void UseStmt::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

void ImportFunctionDecl::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

void Program::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

} // namespace tbx
