#include "parser/parser.hpp"
#include <stdexcept>
#include <set>

namespace tbx {

// Keywords that are NOT parameters in pattern syntax
static const std::set<std::string> reservedWords = {
    "set", "to", "if", "then", "else", "while", "loop", "function", "return",
    "is", "the", "a", "an", "and", "or", "not", "pattern", "syntax", "when",
    "parsed", "triggered", "priority", "import", "class", "members", "created",
    "new", "of", "with", "by", "each", "member", "print", "use", "from",
    "multiply", "add", "subtract", "divide"
};

Parser::Parser(Lexer& lexer) : lexer_(lexer) {
    matcher_ = std::make_unique<PatternMatcher>(registry_);
    advance();
}

void Parser::setSharedRegistry(PatternRegistry* registry) {
    sharedRegistry_ = registry;
    if (sharedRegistry_) {
        matcher_ = std::make_unique<PatternMatcher>(*sharedRegistry_);
    }
}

PatternRegistry& Parser::activeRegistry() {
    return sharedRegistry_ ? *sharedRegistry_ : registry_;
}

void Parser::advance() {
    previous_ = current_;
    current_ = lexer_.next_token();
}

bool Parser::check(TokenType type) {
    return current_.type == type;
}

bool Parser::match(TokenType type) {
    if (check(type)) {
        advance();
        return true;
    }
    return false;
}

Token Parser::consume(TokenType type, const std::string& message) {
    if (check(type)) {
        Token token = current_;
        advance();
        return token;
    }
    throw std::runtime_error(message + " at line " + std::to_string(current_.location.line));
}

std::unique_ptr<Program> Parser::parse() {
    auto program = std::make_unique<Program>();

    while (!check(TokenType::END_OF_FILE)) {
        // Skip empty lines
        while (match(TokenType::NEWLINE)) {}

        if (!check(TokenType::END_OF_FILE)) {
            program->statements.push_back(statement());
        }
    }

    return program;
}

bool Parser::checkAt(size_t pos, TokenType type) {
    if (pos >= tokens_.size()) return false;
    return tokens_[pos].type == type;
}

int Parser::skipNewlinesAndGetIndent() {
    // Skip newlines and count leading whitespace for indentation
    // For now, return 0 - full indentation tracking to be implemented
    while (match(TokenType::NEWLINE)) {}
    return 0;
}

StmtPtr Parser::statement() {
    if (match(TokenType::PATTERN)) {
        return patternDefinition();
    }
    if (match(TokenType::CLASS)) {
        return classDefinition();
    }
    if (match(TokenType::EXPRESSION)) {
        return expressionDefinition();
    }
    if (match(TokenType::EFFECT)) {
        return effectDefinition();
    }
    if (match(TokenType::IMPORT)) {
        return importStatement();
    }
    if (match(TokenType::USE)) {
        return useStatement();
    }

    // For SET, check if it matches built-in pattern: set <IDENTIFIER> to <expr>
    // Built-in set requires: SET followed by IDENTIFIER/RESULT followed by TO
    // Otherwise, it might be a pattern like "set each member to 0" or "set x of result to val"
    if (check(TokenType::SET)) {
        Token next = lexer_.peek();
        Token nextNext = lexer_.peekAhead(1);
        if ((next.type == TokenType::IDENTIFIER || next.type == TokenType::RESULT) &&
            nextNext.type == TokenType::TO) {
            advance();  // consume SET
            return setStatement();
        }
        // Not the built-in pattern, try pattern matching
        auto patternStmt = matchPatternStatement();
        if (patternStmt) {
            return patternStmt;
        }
        // Fallback: consume SET and try setStatement anyway (will likely error)
        advance();
        return setStatement();
    }

    if (match(TokenType::IF)) {
        return ifStatement();
    }
    if (match(TokenType::WHILE)) {
        return whileStatement();
    }
    if (match(TokenType::FUNCTION)) {
        return functionDeclaration();
    }
    if (match(TokenType::PRINT)) {
        // Handle print as a statement
        auto stmt = std::make_unique<ExpressionStmt>();
        auto call = std::make_unique<IntrinsicCall>();
        call->name = "print";
        call->args.push_back(expression());
        stmt->expression = std::move(call);
        return stmt;
    }
    // For MULTIPLY, check if it matches built-in pattern: multiply <IDENTIFIER/RESULT> by <expr>
    // Otherwise, it might be a pattern like "multiply each member of result by scalar"
    if (check(TokenType::MULTIPLY)) {
        Token next = lexer_.peek();
        Token nextNext = lexer_.peekAhead(1);
        if ((next.type == TokenType::IDENTIFIER || next.type == TokenType::RESULT) &&
            nextNext.type == TokenType::BY) {
            advance();  // consume MULTIPLY
            // multiply <var> by <expr>
            // Equivalent to: set <var> to <var> * <expr>
            std::string varName = current_.lexeme;
            advance();  // consume variable name
            consume(TokenType::BY, "Expected 'by' after variable in multiply");
            ExprPtr factor = expression();

            // Create: set <var> to <var> * <factor>
            auto stmt = std::make_unique<SetStatement>();
            stmt->variable = varName;

            auto binary = std::make_unique<BinaryExpr>();
            auto varRef = std::make_unique<Identifier>();
            varRef->name = varName;
            binary->left = std::move(varRef);
            binary->op = TokenType::STAR;
            binary->right = std::move(factor);

            stmt->value = std::move(binary);
            return stmt;
        }
        // Not the built-in pattern, try pattern matching
        auto patternStmt = matchPatternStatement();
        if (patternStmt) {
            return patternStmt;
        }
        // Fallback: consume MULTIPLY and try built-in anyway (will likely error)
        advance();
        throw std::runtime_error("Expected variable name after 'multiply' or no matching pattern found");
    }

    // Try to match against registered patterns
    auto patternStmt = matchPatternStatement();
    if (patternStmt) {
        return patternStmt;
    }

    // Expression statement
    auto stmt = std::make_unique<ExpressionStmt>();
    stmt->expression = expression();
    return stmt;
}

StmtPtr Parser::matchPatternStatement() {
    // Collect tokens until end of line for pattern matching
    std::vector<Token> lineTokens;
    Token startToken = current_;

    // Save current position by storing tokens
    while (!check(TokenType::NEWLINE) && !check(TokenType::END_OF_FILE)) {
        lineTokens.push_back(current_);
        advance();
    }

    if (lineTokens.empty()) {
        return nullptr;
    }

    // Try to match against patterns
    auto result = matcher_->matchStatement(lineTokens, 0);

    if (result && result->pattern && result->pattern->definition) {
        // Create a PatternCall node
        auto call = std::make_unique<PatternCall>();
        call->pattern = result->pattern->definition;
        call->bindings = std::move(result->bindings);
        call->location = startToken.location;

        // Wrap in expression statement
        auto stmt = std::make_unique<ExpressionStmt>();
        stmt->expression = std::move(call);
        return stmt;
    }

    // No pattern matched - reset and return null
    // We need to handle this case by parsing as expression
    // For now, create a natural expression from the tokens
    if (!lineTokens.empty()) {
        auto expr = std::make_unique<NaturalExpr>();
        expr->tokens = std::move(lineTokens);
        expr->location = startToken.location;

        auto stmt = std::make_unique<ExpressionStmt>();
        stmt->expression = std::move(expr);
        return stmt;
    }

    return nullptr;
}

StmtPtr Parser::setStatement() {
    // set <variable> to <expression>
    auto stmt = std::make_unique<SetStatement>();

    // Handle both IDENTIFIER and RESULT keyword as variable names
    std::string varName;
    if (check(TokenType::IDENTIFIER)) {
        varName = current_.lexeme;
        advance();
    } else if (check(TokenType::RESULT)) {
        varName = current_.lexeme;
        advance();
    } else {
        throw std::runtime_error("Expected variable name after 'set'");
    }
    stmt->variable = varName;

    // Register the variable
    activeRegistry().defineVariable(varName);

    consume(TokenType::TO, "Expected 'to' after variable name");
    stmt->value = expression();
    return stmt;
}

StmtPtr Parser::ifStatement() {
    // if <condition> then:
    //     <statements>
    // else:
    //     <statements>
    auto stmt = std::make_unique<IfStatement>();
    stmt->condition = expression();
    consume(TokenType::THEN, "Expected 'then' after condition");
    consume(TokenType::COLON, "Expected ':' after 'then'");

    match(TokenType::NEWLINE);
    stmt->then_branch = parseBlock();

    if (match(TokenType::ELSE)) {
        consume(TokenType::COLON, "Expected ':' after 'else'");
        match(TokenType::NEWLINE);
        stmt->else_branch = parseBlock();
    }

    return stmt;
}

StmtPtr Parser::whileStatement() {
    // while <condition>:
    //     <statements>
    auto stmt = std::make_unique<WhileStatement>();
    stmt->condition = expression();
    consume(TokenType::COLON, "Expected ':' after condition");

    match(TokenType::NEWLINE);
    stmt->body = parseBlock();

    return stmt;
}

StmtPtr Parser::functionDeclaration() {
    auto decl = std::make_unique<FunctionDecl>();
    Token name = consume(TokenType::IDENTIFIER, "Expected function name");
    decl->name = name.lexeme;

    consume(TokenType::COLON, "Expected ':' after function name");
    match(TokenType::NEWLINE);

    decl->body = parseBlock();

    return decl;
}

ExprPtr Parser::expression() {
    return equality();
}

ExprPtr Parser::equality() {
    auto expr = comparison();

    while (match(TokenType::EQUALS) || match(TokenType::NOT_EQUALS)) {
        TokenType op = previous_.type;
        auto right = comparison();
        auto binary = std::make_unique<BinaryExpr>();
        binary->left = std::move(expr);
        binary->op = op;
        binary->right = std::move(right);
        expr = std::move(binary);
    }

    return expr;
}

ExprPtr Parser::comparison() {
    auto expr = term();

    while (match(TokenType::LESS) || match(TokenType::GREATER)) {
        TokenType op = previous_.type;
        auto right = term();
        auto binary = std::make_unique<BinaryExpr>();
        binary->left = std::move(expr);
        binary->op = op;
        binary->right = std::move(right);
        expr = std::move(binary);
    }

    return expr;
}

ExprPtr Parser::term() {
    auto expr = factor();

    while (match(TokenType::PLUS) || match(TokenType::MINUS)) {
        TokenType op = previous_.type;
        auto right = factor();
        auto binary = std::make_unique<BinaryExpr>();
        binary->left = std::move(expr);
        binary->op = op;
        binary->right = std::move(right);
        expr = std::move(binary);
    }

    return expr;
}

ExprPtr Parser::factor() {
    auto expr = primary();

    while (match(TokenType::STAR) || match(TokenType::SLASH)) {
        TokenType op = previous_.type;
        auto right = primary();
        auto binary = std::make_unique<BinaryExpr>();
        binary->left = std::move(expr);
        binary->op = op;
        binary->right = std::move(right);
        expr = std::move(binary);
    }

    return expr;
}

ExprPtr Parser::primary() {
    ExprPtr baseExpr;
    Token baseToken = current_;

    if (match(TokenType::INTEGER)) {
        auto lit = std::make_unique<IntegerLiteral>();
        lit->value = std::get<int64_t>(previous_.value);
        baseExpr = std::move(lit);
    }
    else if (match(TokenType::FLOAT)) {
        auto lit = std::make_unique<FloatLiteral>();
        lit->value = std::get<double>(previous_.value);
        baseExpr = std::move(lit);
    }
    else if (match(TokenType::STRING)) {
        auto lit = std::make_unique<StringLiteral>();
        lit->value = std::get<std::string>(previous_.value);
        baseExpr = std::move(lit);
    }
    else if (match(TokenType::IDENTIFIER)) {
        auto id = std::make_unique<Identifier>();
        id->name = previous_.lexeme;
        baseExpr = std::move(id);
    }
    else if (match(TokenType::AT)) {
        return intrinsicCall();
    }
    else if (match(TokenType::LPAREN)) {
        auto expr = expression();
        consume(TokenType::RPAREN, "Expected ')' after expression");
        return expr;
    }
    // Handle keyword tokens that might appear in expressions
    else if (match(TokenType::A) || match(TokenType::AN) || match(TokenType::THE) ||
        match(TokenType::NEW) || match(TokenType::EACH) || match(TokenType::MEMBER) ||
        match(TokenType::OF) || match(TokenType::WITH) || match(TokenType::BY) ||
        match(TokenType::RESULT)) {
        // These are natural language keywords - create identifier
        auto id = std::make_unique<Identifier>();
        id->name = previous_.lexeme;
        baseExpr = std::move(id);
    }
    // Handle unknown tokens by creating a NaturalExpr
    // This allows natural language patterns to be captured
    else if (!check(TokenType::NEWLINE) && !check(TokenType::END_OF_FILE)) {
        auto expr = std::make_unique<NaturalExpr>();
        expr->tokens.push_back(current_);
        advance();
        return expr;
    }
    else {
        throw std::runtime_error("Unexpected token: " + current_.lexeme);
    }

    // Check for suffix patterns like "var's next power of two"
    // If current token is apostrophe, this might be an expression pattern
    if (check(TokenType::APOSTROPHE)) {
        // Collect remaining tokens on this line to try pattern matching
        std::vector<Token> tokens;
        tokens.push_back(baseToken);  // The primary expression token

        while (!check(TokenType::NEWLINE) && !check(TokenType::END_OF_FILE) &&
               !check(TokenType::RPAREN) && !check(TokenType::COMMA)) {
            tokens.push_back(current_);
            advance();
        }

        // Try to match expression pattern
        auto result = matcher_->matchStatement(tokens, 0);

        if (result && result->pattern && result->pattern->definition) {
            auto call = std::make_unique<PatternCall>();
            call->pattern = result->pattern->definition;
            call->bindings = std::move(result->bindings);
            call->location = baseToken.location;
            return call;
        }
        // No pattern matched - but we've consumed the tokens, so we can't easily go back
        // For now, return the base expression (this is a limitation)
    }

    return baseExpr;
}

ExprPtr Parser::intrinsicCall() {
    // @intrinsic("name", arg1, arg2, ...)
    // We already consumed @, now expect 'intrinsic'
    consume(TokenType::IDENTIFIER, "Expected 'intrinsic' after '@'");
    if (previous_.lexeme != "intrinsic") {
        throw std::runtime_error("Expected 'intrinsic' after '@'");
    }

    consume(TokenType::LPAREN, "Expected '(' after '@intrinsic'");

    auto call = std::make_unique<IntrinsicCall>();

    // First argument is the intrinsic name (string)
    consume(TokenType::STRING, "Expected intrinsic name as string");
    call->name = std::get<std::string>(previous_.value);

    // Parse remaining arguments
    while (match(TokenType::COMMA)) {
        call->args.push_back(expression());
    }

    consume(TokenType::RPAREN, "Expected ')' after intrinsic arguments");

    return call;
}

StmtPtr Parser::patternDefinition() {
    // pattern:
    //     syntax: set var to val
    //     priority: 100
    //     when triggered:
    //         @intrinsic("store", var, val)

    auto def = std::make_unique<PatternDef>();

    consume(TokenType::COLON, "Expected ':' after 'pattern'");
    match(TokenType::NEWLINE);

    // Parse pattern properties (syntax, priority, when triggered)
    while (!check(TokenType::END_OF_FILE)) {
        // Skip blank lines
        while (match(TokenType::NEWLINE)) {}

        // Check if we're at a top-level statement (column 1, not indented)
        if (current_.location.column == 1) {
            // Any token at column 1 that's not a pattern property ends the pattern definition
            // Pattern properties (syntax, when, priority) are indented
            break;
        }

        if (check(TokenType::PATTERN) || check(TokenType::SET) ||
            check(TokenType::IF) || check(TokenType::FUNCTION) ||
            check(TokenType::IMPORT) || check(TokenType::CLASS) ||
            check(TokenType::EXPRESSION) || check(TokenType::END_OF_FILE)) {
            // Start of new top-level statement
            break;
        }

        if (match(TokenType::SYNTAX)) {
            consume(TokenType::COLON, "Expected ':' after 'syntax'");
            def->syntax = parsePatternSyntax();
        }
        else if (match(TokenType::WHEN)) {
            if (match(TokenType::PARSED)) {
                consume(TokenType::COLON, "Expected ':' after 'when parsed'");
                match(TokenType::NEWLINE);
                def->when_parsed = parseBlock();
            }
            else if (match(TokenType::TRIGGERED)) {
                consume(TokenType::COLON, "Expected ':' after 'when triggered'");
                match(TokenType::NEWLINE);
                def->when_triggered = parseBlock();
            }
            else if (match(TokenType::CREATED)) {
                consume(TokenType::COLON, "Expected ':' after 'when created'");
                match(TokenType::NEWLINE);
                // Parse constructor body - for class definitions
                def->when_triggered = parseBlock();
            }
            else {
                throw std::runtime_error("Expected 'parsed', 'triggered', or 'created' after 'when'");
            }
        }
        else if (match(TokenType::PRIORITY)) {
            consume(TokenType::COLON, "Expected ':' after 'priority'");
            // Parse priority value (skip for now)
            while (!check(TokenType::NEWLINE) && !check(TokenType::END_OF_FILE)) {
                advance();
            }
            match(TokenType::NEWLINE);
        }
        else {
            // Unknown property, skip line
            while (!check(TokenType::NEWLINE) && !check(TokenType::END_OF_FILE)) {
                advance();
            }
            match(TokenType::NEWLINE);
        }
    }

    // Register the pattern
    activeRegistry().registerFromDef(def.get());

    return def;
}

StmtPtr Parser::classDefinition() {
    // class:
    //     pattern:
    //         vector
    //     members:
    //         x, y, z
    //     when created:
    //         set each member to 0

    consume(TokenType::COLON, "Expected ':' after 'class'");
    match(TokenType::NEWLINE);

    auto classDef = std::make_unique<ClassDef>();
    std::vector<StmtPtr> classStatements;

    // Parse class body
    while (!check(TokenType::END_OF_FILE)) {
        while (match(TokenType::NEWLINE)) {}

        if (check(TokenType::PATTERN) && !checkAt(tokenPos_ + 1, TokenType::COLON)) {
            break;
        }

        // Check indentation - any token at column 1 (not indented) ends the class body
        if (current_.location.column == 1) {
            break;
        }

        if (check(TokenType::CLASS) || check(TokenType::EXPRESSION) ||
            check(TokenType::EFFECT) || check(TokenType::IMPORT) ||
            check(TokenType::SET) || check(TokenType::IF) ||
            check(TokenType::FUNCTION) || check(TokenType::END_OF_FILE)) {
            // Check if this is a top-level statement (not indented)
            // For now, break on any of these
            break;
        }

        if (match(TokenType::PATTERN) || match(TokenType::PATTERNS)) {
            consume(TokenType::COLON, "Expected ':' after 'pattern'/'patterns'");
            // Parse class pattern (the name/syntax to match for this class)
            while (!check(TokenType::NEWLINE) && !check(TokenType::END_OF_FILE)) {
                if (check(TokenType::IDENTIFIER)) {
                    classDef->name = current_.lexeme;
                    PatternElement elem;
                    elem.is_param = false;
                    elem.is_optional = false;
                    elem.value = current_.lexeme;
                    classDef->patternSyntax.push_back(elem);
                }
                advance();
            }
            match(TokenType::NEWLINE);
        }
        else if (match(TokenType::MEMBERS)) {
            consume(TokenType::COLON, "Expected ':' after 'members'");
            classDef->members = parseIdentifierList();
            match(TokenType::NEWLINE);
        }
        else if (match(TokenType::WHEN)) {
            if (match(TokenType::CREATED)) {
                consume(TokenType::COLON, "Expected ':' after 'when created'");
                match(TokenType::NEWLINE);
                classDef->constructorBody = parseBlock();
            }
            else {
                // Skip unknown when clause
                while (!check(TokenType::NEWLINE) && !check(TokenType::END_OF_FILE)) {
                    advance();
                }
                match(TokenType::NEWLINE);
            }
        }
        else {
            // Skip unknown class property
            while (!check(TokenType::NEWLINE) && !check(TokenType::END_OF_FILE)) {
                advance();
            }
            match(TokenType::NEWLINE);
        }
    }

    // Register the class
    activeRegistry().registerClass(std::move(classDef));

    // Return an empty statement for now (class definitions don't generate code directly)
    auto emptyDef = std::make_unique<PatternDef>();
    return emptyDef;
}

StmtPtr Parser::expressionDefinition() {
    // Supports two forms:
    // 1. expression:
    //        syntax: ...
    //        get: ...
    // 2. expression <syntax>:
    //        get: ...

    auto def = std::make_unique<PatternDef>();

    // Check if next token is colon (old form) or syntax inline (new form)
    if (check(TokenType::COLON)) {
        consume(TokenType::COLON, "Expected ':' after 'expression'");
        match(TokenType::NEWLINE);

        // Parse expression properties
        while (!check(TokenType::END_OF_FILE)) {
            while (match(TokenType::NEWLINE)) {}

            if (check(TokenType::END_OF_FILE)) break;

            bool atColumnOne = (current_.location.column == 1);
            bool isTopLevelKeyword = check(TokenType::PATTERN) || check(TokenType::CLASS) ||
                check(TokenType::EXPRESSION) || check(TokenType::EFFECT) ||
                check(TokenType::FUNCTION) || check(TokenType::IMPORT) || check(TokenType::USE);

            if (atColumnOne && isTopLevelKeyword) break;

            if (match(TokenType::SYNTAX)) {
                consume(TokenType::COLON, "Expected ':' after 'syntax'");
                def->syntax = parsePatternSyntax();
            }
            else if (match(TokenType::GET)) {
                consume(TokenType::COLON, "Expected ':' after 'get'");
                match(TokenType::NEWLINE);
                def->when_triggered = parseBlock();
            }
            else {
                while (!check(TokenType::NEWLINE) && !check(TokenType::END_OF_FILE)) {
                    advance();
                }
                match(TokenType::NEWLINE);
            }
        }
    } else {
        // Inline syntax: expression <syntax>:
        def->syntax = parsePatternSyntaxUntilColon();
        consume(TokenType::COLON, "Expected ':' after expression syntax");
        match(TokenType::NEWLINE);

        // Parse expression body
        while (!check(TokenType::END_OF_FILE)) {
            while (match(TokenType::NEWLINE)) {}

            if (check(TokenType::END_OF_FILE)) break;

            bool atColumnOne = (current_.location.column == 1);
            bool isTopLevelKeyword = check(TokenType::PATTERN) || check(TokenType::CLASS) ||
                check(TokenType::EXPRESSION) || check(TokenType::EFFECT) ||
                check(TokenType::FUNCTION) || check(TokenType::IMPORT) || check(TokenType::USE);

            if (atColumnOne && isTopLevelKeyword) break;

            if (match(TokenType::GET)) {
                consume(TokenType::COLON, "Expected ':' after 'get'");
                match(TokenType::NEWLINE);
                def->when_triggered = parseBlock();
            }
            else {
                while (!check(TokenType::NEWLINE) && !check(TokenType::END_OF_FILE)) {
                    advance();
                }
                match(TokenType::NEWLINE);
            }
        }
    }

    // Register the pattern
    activeRegistry().registerFromDef(def.get());

    return def;
}

StmtPtr Parser::effectDefinition() {
    // effect <syntax>:
    //     when triggered:
    //         ...

    auto def = std::make_unique<PatternDef>();

    // Parse inline syntax: effect <syntax>:
    def->syntax = parsePatternSyntaxUntilColon();
    consume(TokenType::COLON, "Expected ':' after effect syntax");
    match(TokenType::NEWLINE);

    // Parse effect body
    while (!check(TokenType::END_OF_FILE)) {
        while (match(TokenType::NEWLINE)) {}

        if (check(TokenType::END_OF_FILE)) break;

        bool atColumnOne = (current_.location.column == 1);
        bool isTopLevelKeyword = check(TokenType::PATTERN) || check(TokenType::CLASS) ||
            check(TokenType::EXPRESSION) || check(TokenType::EFFECT) ||
            check(TokenType::FUNCTION) || check(TokenType::IMPORT) || check(TokenType::USE);

        if (atColumnOne && isTopLevelKeyword) break;

        if (match(TokenType::WHEN)) {
            if (match(TokenType::TRIGGERED)) {
                consume(TokenType::COLON, "Expected ':' after 'when triggered'");
                match(TokenType::NEWLINE);
                def->when_triggered = parseBlock();
            }
            else {
                while (!check(TokenType::NEWLINE) && !check(TokenType::END_OF_FILE)) {
                    advance();
                }
                match(TokenType::NEWLINE);
            }
        }
        else {
            while (!check(TokenType::NEWLINE) && !check(TokenType::END_OF_FILE)) {
                advance();
            }
            match(TokenType::NEWLINE);
        }
    }

    // Register the pattern
    activeRegistry().registerFromDef(def.get());

    return def;
}

StmtPtr Parser::importStatement() {
    // Check if this is an import function declaration
    // import function name(params) from "header"
    if (check(TokenType::FUNCTION)) {
        return importFunctionDeclaration();
    }

    // import module.3bx
    auto stmt = std::make_unique<ImportStmt>();

    // Parse module path (identifier.3bx or identifier/path.3bx)
    std::string path;
    while (!check(TokenType::NEWLINE) && !check(TokenType::END_OF_FILE)) {
        path += current_.lexeme;
        advance();
    }

    stmt->module_path = path;
    return stmt;
}

StmtPtr Parser::importFunctionDeclaration() {
    // import function name(params) from "header"
    // We already matched IMPORT, now expect FUNCTION
    consume(TokenType::FUNCTION, "Expected 'function' after 'import'");

    auto decl = std::make_unique<ImportFunctionDecl>();

    // Parse function name
    Token name = consume(TokenType::IDENTIFIER, "Expected function name after 'import function'");
    decl->name = name.lexeme;

    // Parse parameter list
    consume(TokenType::LPAREN, "Expected '(' after function name");

    if (!check(TokenType::RPAREN)) {
        do {
            Token param = consume(TokenType::IDENTIFIER, "Expected parameter name");
            decl->params.push_back(param.lexeme);
        } while (match(TokenType::COMMA));
    }

    consume(TokenType::RPAREN, "Expected ')' after parameters");

    // Parse 'from "header"'
    consume(TokenType::FROM, "Expected 'from' after parameter list");
    Token header = consume(TokenType::STRING, "Expected header path string after 'from'");
    decl->header = std::get<std::string>(header.value);

    return decl;
}

StmtPtr Parser::useStatement() {
    // use thing from module.3bx
    auto stmt = std::make_unique<UseStmt>();

    // Parse the item name
    Token item = consume(TokenType::IDENTIFIER, "Expected item name after 'use'");
    stmt->item_name = item.lexeme;

    // Expect 'from'
    consume(TokenType::FROM, "Expected 'from' after item name");

    // Parse module path (identifier.3bx or identifier/path.3bx)
    std::string path;
    while (!check(TokenType::NEWLINE) && !check(TokenType::END_OF_FILE)) {
        path += current_.lexeme;
        advance();
    }

    stmt->module_path = path;
    return stmt;
}

std::vector<PatternElement> Parser::parsePatternSyntax() {
    // Parse: set var to val
    // Words that are in reservedWords become literals, others become parameters
    // Words in brackets like [the] become optional literals
    std::vector<PatternElement> elements;

    while (!check(TokenType::NEWLINE) && !check(TokenType::END_OF_FILE)) {
        PatternElement elem;
        elem.is_optional = false;

        // Check for optional element syntax: [word]
        if (match(TokenType::LBRACKET)) {
            elem.is_optional = true;
            std::string word = current_.lexeme;

            // The word inside brackets is always a literal (optional words are always literals)
            elem.is_param = false;
            elem.value = word;
            advance();

            if (!match(TokenType::RBRACKET)) {
                throw std::runtime_error("Expected ']' after optional element at line " +
                    std::to_string(current_.location.line));
            }

            elements.push_back(elem);
            continue;
        }

        std::string word = current_.lexeme;

        // Check if this is a reserved word (becomes literal) or an identifier/keyword (becomes parameter)
        // Reserved words are literals in patterns (e.g., "set", "to", "if")
        // Non-reserved words (even if they're keywords) become parameters
        bool isReserved = reservedWords.count(word) > 0;

        if (isReserved) {
            // Reserved word -> literal in pattern
            elem.is_param = false;
            elem.value = word;
        }
        else if (current_.type == TokenType::IDENTIFIER ||
                 current_.type == TokenType::RESULT ||
                 current_.type == TokenType::MULTIPLY ||
                 current_.type == TokenType::EFFECT ||
                 current_.type == TokenType::GET ||
                 current_.type == TokenType::PATTERNS) {
            // Identifier or non-reserved keyword -> parameter in pattern
            elem.is_param = true;
            elem.value = word;
        }
        else if (current_.type == TokenType::PLUS ||
                 current_.type == TokenType::MINUS ||
                 current_.type == TokenType::STAR ||
                 current_.type == TokenType::SLASH) {
            // Operators in patterns are literals
            elem.is_param = false;
            elem.value = word;
        }
        else {
            // Skip other tokens
            advance();
            continue;
        }

        elements.push_back(elem);
        advance();
    }

    match(TokenType::NEWLINE);
    return elements;
}

std::vector<PatternElement> Parser::parsePatternSyntaxUntilColon() {
    // Parse syntax elements until we hit a colon
    // For patterns like: var's next power of two
    // The apostrophe followed by 's' creates a possessive pattern
    //
    // Rules for determining parameters vs literals:
    // 1. Reserved words are always literals
    // 2. If an identifier is followed by "'s", it's a parameter (possessive pattern)
    // 3. After "'s", all non-reserved identifiers are LITERALS (they form the pattern name)
    // 4. For non-possessive patterns (like "say message"):
    //    - First identifier is a LITERAL (the pattern name/verb)
    //    - Subsequent identifiers are PARAMETERS
    std::vector<PatternElement> elements;
    bool seenPossessive = false;
    bool seenFirstIdentifier = false;

    while (!check(TokenType::COLON) && !check(TokenType::NEWLINE) && !check(TokenType::END_OF_FILE)) {
        PatternElement elem;
        elem.is_optional = false;

        // Check for optional element syntax: [word]
        if (match(TokenType::LBRACKET)) {
            elem.is_optional = true;
            std::string word = current_.lexeme;
            elem.is_param = false;
            elem.value = word;
            advance();

            if (!match(TokenType::RBRACKET)) {
                throw std::runtime_error("Expected ']' after optional element at line " +
                    std::to_string(current_.location.line));
            }

            elements.push_back(elem);
            continue;
        }

        // Handle apostrophe for possessive (e.g., var's -> param followed by "'s")
        if (check(TokenType::APOSTROPHE)) {
            advance();  // consume apostrophe
            // Check for 's' suffix
            if (check(TokenType::IDENTIFIER) && current_.lexeme == "s") {
                elem.is_param = false;
                elem.value = "'s";
                elements.push_back(elem);
                advance();  // consume 's'
                seenPossessive = true;
                continue;
            }
            // Just add the apostrophe as a literal
            elem.is_param = false;
            elem.value = "'";
            elements.push_back(elem);
            continue;
        }

        std::string word = current_.lexeme;

        // Check if this is a reserved word (becomes literal)
        bool isReserved = reservedWords.count(word) > 0;

        if (isReserved) {
            // Reserved word -> literal in pattern
            elem.is_param = false;
            elem.value = word;
        }
        else if (current_.type == TokenType::IDENTIFIER ||
                 current_.type == TokenType::RESULT ||
                 current_.type == TokenType::MULTIPLY ||
                 current_.type == TokenType::EFFECT ||
                 current_.type == TokenType::GET ||
                 current_.type == TokenType::PATTERNS) {
            // Non-reserved identifier - determine if parameter or literal

            // Check if this identifier is followed by apostrophe (possessive pattern)
            Token next = lexer_.peek();
            bool followedByPossessive = (next.type == TokenType::APOSTROPHE);

            if (followedByPossessive) {
                // Identifier before 's is a parameter (e.g., "var" in "var's")
                elem.is_param = true;
                elem.value = word;
            }
            else if (seenPossessive) {
                // After 's, identifiers are LITERALS (form the pattern name)
                elem.is_param = false;
                elem.value = word;
            }
            else if (!seenFirstIdentifier) {
                // First identifier in non-possessive pattern is a LITERAL (pattern name)
                elem.is_param = false;
                elem.value = word;
                seenFirstIdentifier = true;
            }
            else {
                // Subsequent identifiers before any marker are PARAMETERS
                elem.is_param = true;
                elem.value = word;
            }
        }
        else if (current_.type == TokenType::PLUS ||
                 current_.type == TokenType::MINUS ||
                 current_.type == TokenType::STAR ||
                 current_.type == TokenType::SLASH) {
            // Operators in patterns are literals
            elem.is_param = false;
            elem.value = word;
        }
        else {
            // Skip other tokens
            advance();
            continue;
        }

        elements.push_back(elem);
        advance();
    }

    return elements;
}

std::vector<std::string> Parser::parseIdentifierList() {
    std::vector<std::string> identifiers;

    // Parse comma-separated list of identifiers
    while (!check(TokenType::NEWLINE) && !check(TokenType::END_OF_FILE)) {
        if (check(TokenType::IDENTIFIER)) {
            identifiers.push_back(current_.lexeme);
            advance();
        }
        else if (match(TokenType::COMMA)) {
            // Skip comma
            continue;
        }
        else {
            advance();
        }
    }

    return identifiers;
}

std::vector<StmtPtr> Parser::parseBlock() {
    // Parse indented block: all statements with indentation >= block's base level
    std::vector<StmtPtr> statements;

    // Skip leading newlines
    while (match(TokenType::NEWLINE)) {}

    // Record the indentation level of the first statement in the block
    size_t blockIndent = current_.location.column;

    // Parse statements while they are indented at the block level
    while (!check(TokenType::END_OF_FILE)) {
        // Skip blank lines
        while (match(TokenType::NEWLINE)) {}

        if (check(TokenType::END_OF_FILE)) {
            break;
        }

        // Check if current token is at the block's indentation level or deeper
        size_t currentIndent = current_.location.column;
        if (currentIndent < blockIndent) {
            // Dedented - end of block
            break;
        }

        // Check for keywords that indicate we're outside the block
        // (pattern sections like syntax:, when:, priority:, members:)
        if (check(TokenType::SYNTAX) ||
            check(TokenType::WHEN) ||
            check(TokenType::PRIORITY) ||
            check(TokenType::MEMBERS)) {
            break;
        }

        // Check for top-level keywords at column 1 (not indented)
        if (currentIndent == 1 &&
            (check(TokenType::PATTERN) ||
             check(TokenType::CLASS) ||
             check(TokenType::EXPRESSION) ||
             check(TokenType::IMPORT) ||
             check(TokenType::USE) ||
             check(TokenType::FUNCTION))) {
            break;
        }

        statements.push_back(statement());
    }

    return statements;
}

std::vector<StmtPtr> Parser::parseBlockWithIndent(int baseIndent) {
    // Parse statements with proper indentation tracking
    // For now, delegate to parseBlock
    return parseBlock();
}

} // namespace tbx
