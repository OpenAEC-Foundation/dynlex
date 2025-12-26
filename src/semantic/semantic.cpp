#include "semantic/semantic.hpp"
#include "pattern/pattern_registry.hpp"

namespace tbx {

std::string type_to_string(Type type) {
    switch (type) {
        case Type::VOID:     return "void";
        case Type::INTEGER:  return "integer";
        case Type::FLOAT:    return "float";
        case Type::STRING:   return "string";
        case Type::BOOLEAN:  return "boolean";
        case Type::FUNCTION: return "function";
        case Type::UNKNOWN:  return "unknown";
        default:             return "?";
    }
}

SemanticAnalyzer::SemanticAnalyzer() {
    push_scope(); // Global scope
}

bool SemanticAnalyzer::analyze(Program& program) {
    program.accept(*this);
    return errors_.empty();
}

void SemanticAnalyzer::push_scope() {
    scopes_.emplace_back();
}

void SemanticAnalyzer::pop_scope() {
    scopes_.pop_back();
}

void SemanticAnalyzer::define(const std::string& name, Type type, bool mutable_) {
    scopes_.back()[name] = {name, type, mutable_};
}

Symbol* SemanticAnalyzer::lookup(const std::string& name) {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        auto found = it->find(name);
        if (found != it->end()) {
            return &found->second;
        }
    }
    return nullptr;
}

void SemanticAnalyzer::error(const std::string& message) {
    errors_.push_back(message);
}

void SemanticAnalyzer::visit(IntegerLiteral& node) {
    (void)node;
    last_type_ = Type::INTEGER;
}

void SemanticAnalyzer::visit(FloatLiteral& node) {
    (void)node;
    last_type_ = Type::FLOAT;
}

void SemanticAnalyzer::visit(StringLiteral& node) {
    (void)node;
    last_type_ = Type::STRING;
}

void SemanticAnalyzer::visit(Identifier& node) {
    // Reserved words in natural language expressions are not variables
    if (PatternRegistry::isReservedWord(node.name)) {
        last_type_ = Type::UNKNOWN;
        return;
    }

    Symbol* sym = lookup(node.name);
    if (!sym) {
        error("Undefined variable: " + node.name);
        last_type_ = Type::UNKNOWN;
    } else {
        last_type_ = sym->type;
    }
}

void SemanticAnalyzer::visit(BinaryExpr& node) {
    node.left->accept(*this);
    Type left_type = last_type_;

    node.right->accept(*this);
    Type right_type = last_type_;

    // Basic type checking
    if (left_type != right_type && left_type != Type::UNKNOWN && right_type != Type::UNKNOWN) {
        error("Type mismatch in binary expression");
    }

    // Result type depends on operator
    switch (node.op) {
        case TokenType::EQUALS:
        case TokenType::NOT_EQUALS:
        case TokenType::LESS:
        case TokenType::GREATER:
            last_type_ = Type::BOOLEAN;
            break;
        default:
            last_type_ = left_type;
    }
}

void SemanticAnalyzer::visit(NaturalExpr& node) {
    // Natural language expressions are resolved at runtime
    // through pattern matching. For now, treat as unknown type.
    (void)node;
    last_type_ = Type::UNKNOWN;
}

void SemanticAnalyzer::visit(ExpressionStmt& node) {
    node.expression->accept(*this);
}

void SemanticAnalyzer::visit(SetStatement& node) {
    node.value->accept(*this);
    Type value_type = last_type_;

    Symbol* existing = lookup(node.variable);
    if (existing) {
        if (!existing->is_mutable) {
            error("Cannot reassign immutable variable: " + node.variable);
        }
        if (existing->type != value_type && existing->type != Type::UNKNOWN) {
            error("Type mismatch in assignment to: " + node.variable);
        }
    } else {
        define(node.variable, value_type);
    }
}

void SemanticAnalyzer::visit(IfStatement& node) {
    node.condition->accept(*this);

    push_scope();
    for (auto& stmt : node.then_branch) {
        stmt->accept(*this);
    }
    pop_scope();

    if (!node.else_branch.empty()) {
        push_scope();
        for (auto& stmt : node.else_branch) {
            stmt->accept(*this);
        }
        pop_scope();
    }
}

void SemanticAnalyzer::visit(WhileStatement& node) {
    node.condition->accept(*this);

    push_scope();
    for (auto& stmt : node.body) {
        stmt->accept(*this);
    }
    pop_scope();
}

void SemanticAnalyzer::visit(FunctionDecl& node) {
    define(node.name, Type::FUNCTION, false);

    push_scope();
    for (const auto& param : node.params) {
        define(param, Type::UNKNOWN);
    }
    for (auto& stmt : node.body) {
        stmt->accept(*this);
    }
    pop_scope();
}

void SemanticAnalyzer::visit(IntrinsicCall& node) {
    // Analyze intrinsic arguments
    for (auto& arg : node.args) {
        arg->accept(*this);
    }
    // Intrinsics return unknown type for now (will be refined later)
    last_type_ = Type::UNKNOWN;
}

void SemanticAnalyzer::visit(PatternDef& node) {
    // Skip semantic analysis inside pattern definitions.
    // Pattern parameters and local variables are bound at pattern match time,
    // not at compile time. The semantic analyzer cannot properly validate them
    // since they depend on runtime pattern matching.
    (void)node;
}

void SemanticAnalyzer::visit(PatternCall& node) {
    // Analyze bound arguments
    for (auto& [name, expr] : node.bindings) {
        expr->accept(*this);
    }
    last_type_ = Type::UNKNOWN;
}

void SemanticAnalyzer::visit(ImportStmt& node) {
    // Import statements are handled during parsing/loading
    (void)node;
}

void SemanticAnalyzer::visit(UseStmt& node) {
    // Use statements are handled during parsing/loading
    (void)node;
}

void SemanticAnalyzer::visit(ImportFunctionDecl& node) {
    // Import function declarations are handled during parsing/loading
    (void)node;
}

void SemanticAnalyzer::visit(Program& node) {
    for (auto& stmt : node.statements) {
        stmt->accept(*this);
    }
}

} // namespace tbx
