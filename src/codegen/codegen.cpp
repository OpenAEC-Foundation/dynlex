#include "codegen/codegen.hpp"

#include <llvm/Support/FileSystem.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/IR/Verifier.h>

namespace tbx {

CodeGenerator::CodeGenerator(const std::string& module_name) {
    context_ = std::make_unique<llvm::LLVMContext>();
    module_ = std::make_unique<llvm::Module>(module_name, *context_);
    builder_ = std::make_unique<llvm::IRBuilder<>>(*context_);
}

bool CodeGenerator::generate(Program& program) {
    program.accept(*this);
    return !llvm::verifyModule(*module_, &llvm::errs());
}

bool CodeGenerator::write_ir(const std::string& filename) {
    std::error_code ec;
    llvm::raw_fd_ostream out(filename, ec, llvm::sys::fs::OF_None);
    if (ec) {
        return false;
    }
    module_->print(out, nullptr);
    return true;
}

bool CodeGenerator::compile(const std::string& filename) {
    // TODO: Implement native code generation
    (void)filename;
    return false;
}

llvm::AllocaInst* CodeGenerator::create_entry_block_alloca(
    llvm::Function* function,
    const std::string& name,
    llvm::Type* type
) {
    llvm::IRBuilder<> tmp_builder(
        &function->getEntryBlock(),
        function->getEntryBlock().begin()
    );
    return tmp_builder.CreateAlloca(type, nullptr, name);
}

void CodeGenerator::visit(IntegerLiteral& node) {
    current_value_ = llvm::ConstantInt::get(
        llvm::Type::getInt64Ty(*context_),
        node.value,
        true
    );
}

void CodeGenerator::visit(FloatLiteral& node) {
    current_value_ = llvm::ConstantFP::get(
        llvm::Type::getDoubleTy(*context_),
        node.value
    );
}

void CodeGenerator::visit(StringLiteral& node) {
    current_value_ = builder_->CreateGlobalStringPtr(node.value);
}

void CodeGenerator::visit(Identifier& node) {
    auto it = named_values_.find(node.name);
    if (it != named_values_.end()) {
        current_value_ = builder_->CreateLoad(
            it->second->getAllocatedType(),
            it->second,
            node.name
        );
    } else {
        current_value_ = nullptr;
    }
}

void CodeGenerator::visit(BinaryExpr& node) {
    node.left->accept(*this);
    llvm::Value* left = current_value_;

    node.right->accept(*this);
    llvm::Value* right = current_value_;

    if (!left || !right) {
        current_value_ = nullptr;
        return;
    }

    // Check if either operand is a float
    bool left_is_float = left->getType()->isFloatingPointTy();
    bool right_is_float = right->getType()->isFloatingPointTy();
    bool use_float = left_is_float || right_is_float;

    // Convert to float if needed for mixed operations
    if (use_float) {
        llvm::Type* double_type = llvm::Type::getDoubleTy(*context_);
        if (!left_is_float) {
            left = builder_->CreateSIToFP(left, double_type, "conv");
        }
        if (!right_is_float) {
            right = builder_->CreateSIToFP(right, double_type, "conv");
        }
    }

    switch (node.op) {
        case TokenType::PLUS:
            current_value_ = use_float
                ? builder_->CreateFAdd(left, right, "addtmp")
                : builder_->CreateAdd(left, right, "addtmp");
            break;
        case TokenType::MINUS:
            current_value_ = use_float
                ? builder_->CreateFSub(left, right, "subtmp")
                : builder_->CreateSub(left, right, "subtmp");
            break;
        case TokenType::STAR:
            current_value_ = use_float
                ? builder_->CreateFMul(left, right, "multmp")
                : builder_->CreateMul(left, right, "multmp");
            break;
        case TokenType::SLASH:
            current_value_ = use_float
                ? builder_->CreateFDiv(left, right, "divtmp")
                : builder_->CreateSDiv(left, right, "divtmp");
            break;
        case TokenType::LESS:
            current_value_ = use_float
                ? builder_->CreateFCmpOLT(left, right, "cmptmp")
                : builder_->CreateICmpSLT(left, right, "cmptmp");
            break;
        case TokenType::GREATER:
            current_value_ = use_float
                ? builder_->CreateFCmpOGT(left, right, "cmptmp")
                : builder_->CreateICmpSGT(left, right, "cmptmp");
            break;
        case TokenType::EQUALS:
            current_value_ = use_float
                ? builder_->CreateFCmpOEQ(left, right, "eqtmp")
                : builder_->CreateICmpEQ(left, right, "eqtmp");
            break;
        case TokenType::NOT_EQUALS:
            current_value_ = use_float
                ? builder_->CreateFCmpONE(left, right, "netmp")
                : builder_->CreateICmpNE(left, right, "netmp");
            break;
        default:
            current_value_ = nullptr;
    }
}

void CodeGenerator::visit(NaturalExpr& node) {
    // Natural language expressions need pattern matching to resolve
    // For now, generate a placeholder value (0)
    // TODO: Implement pattern matching at compile time
    (void)node;
    current_value_ = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context_), 0);
}

void CodeGenerator::visit(ExpressionStmt& node) {
    node.expression->accept(*this);
}

void CodeGenerator::visit(SetStatement& node) {
    node.value->accept(*this);
    llvm::Value* value = current_value_;
    if (!value) return;

    auto it = named_values_.find(node.variable);
    if (it != named_values_.end()) {
        builder_->CreateStore(value, it->second);
    } else {
        // Create new variable
        llvm::Function* function = builder_->GetInsertBlock()->getParent();
        llvm::AllocaInst* alloca = create_entry_block_alloca(
            function,
            node.variable,
            value->getType()
        );
        builder_->CreateStore(value, alloca);
        named_values_[node.variable] = alloca;
    }
}

void CodeGenerator::visit(IfStatement& node) {
    node.condition->accept(*this);
    llvm::Value* cond = current_value_;
    if (!cond) return;

    llvm::Function* function = builder_->GetInsertBlock()->getParent();

    llvm::BasicBlock* then_bb = llvm::BasicBlock::Create(*context_, "then", function);
    llvm::BasicBlock* else_bb = llvm::BasicBlock::Create(*context_, "else");
    llvm::BasicBlock* merge_bb = llvm::BasicBlock::Create(*context_, "ifcont");

    builder_->CreateCondBr(cond, then_bb, else_bb);

    // Then block
    builder_->SetInsertPoint(then_bb);
    for (auto& stmt : node.then_branch) {
        stmt->accept(*this);
    }
    builder_->CreateBr(merge_bb);

    // Else block
    function->insert(function->end(), else_bb);
    builder_->SetInsertPoint(else_bb);
    for (auto& stmt : node.else_branch) {
        stmt->accept(*this);
    }
    builder_->CreateBr(merge_bb);

    // Merge block
    function->insert(function->end(), merge_bb);
    builder_->SetInsertPoint(merge_bb);
}

void CodeGenerator::visit(WhileStatement& node) {
    llvm::Function* function = builder_->GetInsertBlock()->getParent();

    llvm::BasicBlock* cond_bb = llvm::BasicBlock::Create(*context_, "while.cond", function);
    llvm::BasicBlock* body_bb = llvm::BasicBlock::Create(*context_, "while.body");
    llvm::BasicBlock* end_bb = llvm::BasicBlock::Create(*context_, "while.end");

    // Branch to condition block
    builder_->CreateBr(cond_bb);

    // Condition block
    builder_->SetInsertPoint(cond_bb);
    node.condition->accept(*this);
    llvm::Value* cond = current_value_;
    if (!cond) {
        cond = llvm::ConstantInt::get(llvm::Type::getInt1Ty(*context_), 0);
    }
    // Convert to boolean if needed (compare with 0)
    if (!cond->getType()->isIntegerTy(1)) {
        if (cond->getType()->isIntegerTy()) {
            cond = builder_->CreateICmpNE(cond, llvm::ConstantInt::get(cond->getType(), 0), "tobool");
        } else if (cond->getType()->isDoubleTy()) {
            cond = builder_->CreateFCmpONE(cond, llvm::ConstantFP::get(cond->getType(), 0.0), "tobool");
        }
    }
    builder_->CreateCondBr(cond, body_bb, end_bb);

    // Body block
    function->insert(function->end(), body_bb);
    builder_->SetInsertPoint(body_bb);
    for (auto& stmt : node.body) {
        stmt->accept(*this);
    }
    builder_->CreateBr(cond_bb);  // Loop back to condition

    // End block
    function->insert(function->end(), end_bb);
    builder_->SetInsertPoint(end_bb);
}

void CodeGenerator::visit(FunctionDecl& node) {
    // Create function type
    std::vector<llvm::Type*> param_types(node.params.size(), llvm::Type::getInt64Ty(*context_));
    llvm::FunctionType* func_type = llvm::FunctionType::get(
        llvm::Type::getInt64Ty(*context_),
        param_types,
        false
    );

    llvm::Function* function = llvm::Function::Create(
        func_type,
        llvm::Function::ExternalLinkage,
        node.name,
        module_.get()
    );

    // Set parameter names
    size_t idx = 0;
    for (auto& arg : function->args()) {
        arg.setName(node.params[idx++]);
    }

    // Create entry block
    llvm::BasicBlock* entry = llvm::BasicBlock::Create(*context_, "entry", function);
    builder_->SetInsertPoint(entry);

    // Add parameters to symbol table
    named_values_.clear();
    for (auto& arg : function->args()) {
        llvm::AllocaInst* alloca = create_entry_block_alloca(
            function,
            std::string(arg.getName()),
            arg.getType()
        );
        builder_->CreateStore(&arg, alloca);
        named_values_[std::string(arg.getName())] = alloca;
    }

    // Generate body
    for (auto& stmt : node.body) {
        stmt->accept(*this);
    }

    // Default return
    builder_->CreateRet(llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context_), 0));
}

void CodeGenerator::visit(IntrinsicCall& node) {
    // Map intrinsic names to LLVM operations
    if (node.name == "store") {
        // @intrinsic("store", var_name, value)
        if (node.args.size() >= 2) {
            // Get variable name
            auto* id = dynamic_cast<Identifier*>(node.args[0].get());
            if (id) {
                node.args[1]->accept(*this);
                llvm::Value* value = current_value_;
                if (value) {
                    auto it = named_values_.find(id->name);
                    if (it != named_values_.end()) {
                        builder_->CreateStore(value, it->second);
                    } else {
                        llvm::Function* function = builder_->GetInsertBlock()->getParent();
                        llvm::AllocaInst* alloca = create_entry_block_alloca(
                            function, id->name, value->getType());
                        builder_->CreateStore(value, alloca);
                        named_values_[id->name] = alloca;
                    }
                }
            }
        }
        current_value_ = nullptr;
    }
    else if (node.name == "load") {
        if (node.args.size() >= 1) {
            auto* id = dynamic_cast<Identifier*>(node.args[0].get());
            if (id) {
                auto it = named_values_.find(id->name);
                if (it != named_values_.end()) {
                    current_value_ = builder_->CreateLoad(
                        it->second->getAllocatedType(), it->second, id->name);
                } else {
                    current_value_ = nullptr;
                }
            }
        }
    }
    else if (node.name == "add") {
        if (node.args.size() >= 2) {
            node.args[0]->accept(*this);
            llvm::Value* left = current_value_;
            node.args[1]->accept(*this);
            llvm::Value* right = current_value_;
            if (left && right) {
                current_value_ = builder_->CreateAdd(left, right, "addtmp");
            }
        }
    }
    else if (node.name == "sub") {
        if (node.args.size() >= 2) {
            node.args[0]->accept(*this);
            llvm::Value* left = current_value_;
            node.args[1]->accept(*this);
            llvm::Value* right = current_value_;
            if (left && right) {
                current_value_ = builder_->CreateSub(left, right, "subtmp");
            }
        }
    }
    else if (node.name == "mul") {
        if (node.args.size() >= 2) {
            node.args[0]->accept(*this);
            llvm::Value* left = current_value_;
            node.args[1]->accept(*this);
            llvm::Value* right = current_value_;
            if (left && right) {
                current_value_ = builder_->CreateMul(left, right, "multmp");
            }
        }
    }
    else if (node.name == "div") {
        if (node.args.size() >= 2) {
            node.args[0]->accept(*this);
            llvm::Value* left = current_value_;
            node.args[1]->accept(*this);
            llvm::Value* right = current_value_;
            if (left && right) {
                current_value_ = builder_->CreateSDiv(left, right, "divtmp");
            }
        }
    }
    else if (node.name == "print") {
        // Get or create printf declaration
        llvm::FunctionType* printf_type = llvm::FunctionType::get(
            llvm::Type::getInt32Ty(*context_),
            {llvm::PointerType::get(llvm::Type::getInt8Ty(*context_), 0)},
            true
        );
        llvm::FunctionCallee printf_func = module_->getOrInsertFunction("printf", printf_type);

        if (node.args.size() >= 1) {
            node.args[0]->accept(*this);
            llvm::Value* val = current_value_;
            if (val) {
                llvm::Value* format_str;
                // Choose format based on value type
                if (val->getType()->isPointerTy()) {
                    format_str = builder_->CreateGlobalStringPtr("%s\n");
                } else if (val->getType()->isDoubleTy() || val->getType()->isFloatTy()) {
                    format_str = builder_->CreateGlobalStringPtr("%f\n");
                } else {
                    format_str = builder_->CreateGlobalStringPtr("%lld\n");
                }
                builder_->CreateCall(printf_func, {format_str, val});
            }
        }
        current_value_ = nullptr;
    }
    else if (node.name == "call") {
        // @intrinsic("call", "LIBRARY", "functionName", arg1, arg2, ...)
        // First argument is the library name (string)
        // Second argument is the function name (string)
        // Remaining arguments are function arguments
        if (node.args.size() < 2) {
            current_value_ = nullptr;
            return;
        }

        // Get library name from first argument
        auto* libNameLit = dynamic_cast<StringLiteral*>(node.args[0].get());
        if (!libNameLit) {
            current_value_ = nullptr;
            return;
        }
        std::string libName = libNameLit->value;

        // Get function name from second argument
        auto* funcNameLit = dynamic_cast<StringLiteral*>(node.args[1].get());
        if (!funcNameLit) {
            current_value_ = nullptr;
            return;
        }
        std::string funcName = funcNameLit->value;

        // Track the library for linking
        usedLibraries_.insert(libName);

        // Look up the imported function
        auto it = importedFunctions_.find(funcName);
        if (it == importedFunctions_.end()) {
            // Function not imported - declare it with the argument count
            size_t argCount = node.args.size() - 2;
            declareExternalFunction(funcName, argCount);
            it = importedFunctions_.find(funcName);
            if (it == importedFunctions_.end()) {
                current_value_ = nullptr;
                return;
            }
        }

        llvm::Function* func = it->second.llvmFunc;
        if (!func) {
            current_value_ = nullptr;
            return;
        }

        // Evaluate and collect arguments (skip library and function name)
        std::vector<llvm::Value*> args;
        for (size_t i = 2; i < node.args.size(); i++) {
            node.args[i]->accept(*this);
            if (!current_value_) {
                current_value_ = nullptr;
                return;
            }

            llvm::Value* argVal = current_value_;

            // Convert argument to expected type (double for FFI)
            llvm::Type* expectedType = llvm::Type::getDoubleTy(*context_);
            if (argVal->getType() != expectedType) {
                if (argVal->getType()->isIntegerTy()) {
                    argVal = builder_->CreateSIToFP(argVal, expectedType, "conv");
                } else if (argVal->getType()->isFloatTy()) {
                    argVal = builder_->CreateFPExt(argVal, expectedType, "conv");
                }
            }

            args.push_back(argVal);
        }

        // Call the function
        current_value_ = builder_->CreateCall(func, args, "calltmp");
    }
    else {
        // Unknown intrinsic
        current_value_ = nullptr;
    }
}

void CodeGenerator::visit(PatternDef& node) {
    // Pattern definitions are not directly compiled
    // They are used by the pattern matcher during parsing
    (void)node;
}

void CodeGenerator::visit(PatternCall& node) {
    // Execute the pattern's when_triggered body with bindings
    // For now, substitute bindings into the pattern's body

    // Save current named_values
    auto saved_values = named_values_;

    // Add pattern bindings to named_values
    for (auto& [name, expr] : node.bindings) {
        expr->accept(*this);
        if (current_value_) {
            llvm::Function* function = builder_->GetInsertBlock()->getParent();
            llvm::AllocaInst* alloca = create_entry_block_alloca(
                function, name, current_value_->getType());
            builder_->CreateStore(current_value_, alloca);
            named_values_[name] = alloca;
        }
    }

    // Generate code for when_triggered
    if (node.pattern) {
        for (auto& stmt : node.pattern->when_triggered) {
            stmt->accept(*this);
        }
    }

    // For expression patterns, return the 'result' variable value
    // This allows patterns to return computed values
    auto result_it = named_values_.find("result");
    if (result_it != named_values_.end()) {
        current_value_ = builder_->CreateLoad(
            result_it->second->getAllocatedType(),
            result_it->second,
            "result_val");
    } else {
        current_value_ = nullptr;
    }

    // Restore named_values
    named_values_ = saved_values;
}

void CodeGenerator::visit(ImportStmt& node) {
    // Imports are handled during parsing
    (void)node;
}

void CodeGenerator::visit(UseStmt& node) {
    // Use statements are handled during parsing
    (void)node;
}

void CodeGenerator::visit(ImportFunctionDecl& node) {
    // Register the imported function for later use by the "call" intrinsic
    ImportedFunction imported;
    imported.name = node.name;
    imported.params = node.params;
    imported.header = node.header;

    // Create LLVM function declaration
    imported.llvmFunc = declareExternalFunction(node.name, node.params.size());

    importedFunctions_[node.name] = imported;
}

llvm::Function* CodeGenerator::declareExternalFunction(const std::string& name, size_t paramCount) {
    // Check if already declared in module
    llvm::Function* func = module_->getFunction(name);
    if (!func) {
        // Create function type with double parameters (common for graphics APIs)
        // For flexibility, we use double for all parameters since it can represent
        // both integers and floating point values
        std::vector<llvm::Type*> paramTypes(paramCount, llvm::Type::getDoubleTy(*context_));

        // Return type is double by default (can be extended later for type info)
        llvm::FunctionType* funcType = llvm::FunctionType::get(
            llvm::Type::getDoubleTy(*context_),
            paramTypes,
            false
        );

        // Create external function declaration
        func = llvm::Function::Create(
            funcType,
            llvm::Function::ExternalLinkage,
            name,
            module_.get()
        );
    }

    // Register in importedFunctions_ if not already there
    if (importedFunctions_.find(name) == importedFunctions_.end()) {
        ImportedFunction imported;
        imported.name = name;
        imported.llvmFunc = func;
        importedFunctions_[name] = imported;
    }

    return func;
}

void CodeGenerator::visit(Program& node) {
    // Create main function for top-level statements
    llvm::FunctionType* main_type = llvm::FunctionType::get(
        llvm::Type::getInt32Ty(*context_),
        {},
        false
    );

    llvm::Function* main_func = llvm::Function::Create(
        main_type,
        llvm::Function::ExternalLinkage,
        "main",
        module_.get()
    );

    llvm::BasicBlock* entry = llvm::BasicBlock::Create(*context_, "entry", main_func);
    builder_->SetInsertPoint(entry);

    // Generate code for all statements
    for (auto& stmt : node.statements) {
        // Skip pattern definitions (they're just metadata)
        if (dynamic_cast<PatternDef*>(stmt.get())) {
            continue;
        }
        stmt->accept(*this);
    }

    // Return 0 from main
    builder_->CreateRet(llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context_), 0));
}

} // namespace tbx
