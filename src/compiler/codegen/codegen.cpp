#include "codegen.h"
#include "expression.h"
#include "patternDefinition.h"
#include "patternReference.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <unordered_map>

// Map from pattern sections to their generated LLVM functions
static std::unordered_map<Section *, llvm::Function *> sectionFunctions;

// Current bindings for pattern variables (maps variable name to LLVM value)
static std::unordered_map<std::string, llvm::Value *> patternBindings;

// Forward declarations
static bool generatePatternFunctions(ParseContext &context, Section *section);
static bool generateSectionCode(ParseContext &context, Section *section);
static llvm::Value *generateExpressionCode(ParseContext &context, Expression *expr);
static llvm::Value *
generateIntrinsicCode(ParseContext &context, const std::string &name, const std::vector<llvm::Value *> &args);

// Get the LLVM type for values (i64 for now)
static llvm::Type *getValueType(ParseContext &context) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	return builder.getInt64Ty();
}

// Get the LLVM type for value pointers (i64*)
static llvm::Type *getValuePtrType(ParseContext &context) { return llvm::PointerType::getUnqual(getValueType(context)); }

// Generate a unique function name for a pattern
static std::string getPatternFunctionName(Section *section) {
	// Use section address as unique identifier for now
	// TODO: Generate readable names from pattern text
	std::string name = (section->type == SectionType::Effect) ? "effect_" : "expr_";
	name += std::to_string(reinterpret_cast<uintptr_t>(section));
	return name;
}

// Get variable names from a pattern definition (only actual Variable parameters, not literal words)
static std::vector<std::string> getPatternVariables(Section *section) {
	std::vector<std::string> variables;
	for (PatternDefinition *def : section->patternDefinitions) {
		for (const PatternElement &elem : def->patternElements) {
			if (elem.type == PatternElement::Type::Variable) {
				variables.push_back(elem.text);
			}
		}
	}
	return variables;
}

// Generate LLVM function for a pattern definition (Effect or Expression)
static llvm::Function *generatePatternFunction(ParseContext &context, Section *section) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);

	// Get pattern variables - these become function parameters
	std::vector<std::string> varNames = getPatternVariables(section);

	// Build function type: all parameters are i64* (pass by reference)
	std::vector<llvm::Type *> paramTypes(varNames.size(), getValuePtrType(context));

	// Return type: void for effects, i64 for expressions
	llvm::Type *returnType = (section->type == SectionType::Effect) ? builder.getVoidTy() : getValueType(context);

	llvm::FunctionType *funcType = llvm::FunctionType::get(returnType, paramTypes, false);

	// Create the function
	std::string funcName = getPatternFunctionName(section);
	llvm::Function *func = llvm::Function::Create(funcType, llvm::Function::InternalLinkage, funcName, context.llvmModule);

	// Name the parameters
	size_t idx = 0;
	for (auto &arg : func->args()) {
		arg.setName(varNames[idx++]);
	}

	// Create entry block
	llvm::BasicBlock *entry = llvm::BasicBlock::Create(*context.llvmContext, "entry", func);

	// Save current insert point
	llvm::BasicBlock *savedBlock = builder.GetInsertBlock();
	llvm::BasicBlock::iterator savedPoint = builder.GetInsertPoint();

	// Set insert point to new function
	builder.SetInsertPoint(entry);

	// Set up pattern bindings - map variable names to function parameters
	patternBindings.clear();
	idx = 0;
	for (auto &arg : func->args()) {
		patternBindings[varNames[idx++]] = &arg;
	}

	// Generate code for the body (execute: or get: child section)
	llvm::Value *returnValue = nullptr;
	for (Section *child : section->children) {
		// Generate code for each line in the child section
		for (CodeLine *line : child->codeLines) {
			if (line->expression) {
				llvm::Value *val = generateExpressionCode(context, line->expression);
				// For expressions, the last value is the return value
				if (section->type == SectionType::Expression && val) {
					returnValue = val;
				}
			}
		}
	}

	// Add return statement
	if (section->type == SectionType::Effect) {
		builder.CreateRetVoid();
	} else {
		if (returnValue) {
			builder.CreateRet(returnValue);
		} else {
			// Default return 0 if no value
			builder.CreateRet(builder.getInt64(0));
		}
	}

	// Clear pattern bindings
	patternBindings.clear();

	// Restore insert point
	if (savedBlock) {
		builder.SetInsertPoint(savedBlock, savedPoint);
	}

	return func;
}

// Recursively generate functions for all pattern definitions in a section tree
static bool generatePatternFunctions(ParseContext &context, Section *section) {
	// If this section is an Effect or Expression with pattern definitions, generate a function
	// Skip macros - they are inlined at call site
	if ((section->type == SectionType::Effect || section->type == SectionType::Expression) &&
		!section->patternDefinitions.empty() && !section->isMacro) {
		llvm::Function *func = generatePatternFunction(context, section);
		if (!func) {
			return false;
		}
		sectionFunctions[section] = func;
	}

	// Recurse into children (but skip execute/get sections - they're handled by the parent)
	for (Section *child : section->children) {
		if (child->type != SectionType::Custom) {
			if (!generatePatternFunctions(context, child)) {
				return false;
			}
		}
	}

	return true;
}

// Allocate all variables for a section at its start
static void allocateSectionVariables(ParseContext &context, Section *section) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);

	for (auto &[name, varDef] : section->variableDefinitions) {
		// Allocate stack space for the variable
		llvm::AllocaInst *alloca = builder.CreateAlloca(getValueType(context), nullptr, name);
		context.llvmVariables[name] = alloca;
	}
}

// Get the pointer for a variable expression (for store operations)
static llvm::Value *getVariablePointer(ParseContext &context, Expression *expr) {
	if (!expr || expr->kind != Expression::Kind::Variable || !expr->variable) {
		return nullptr;
	}

	std::string varName = expr->variable->name;

	// First check pattern bindings (function parameters - already pointers)
	auto bindingIt = patternBindings.find(varName);
	if (bindingIt != patternBindings.end()) {
		return bindingIt->second;
	}

	// Check local variables
	auto varIt = context.llvmVariables.find(varName);
	if (varIt != context.llvmVariables.end()) {
		return varIt->second;
	}

	return nullptr;
}

// Generate code for an expression
static llvm::Value *generateExpressionCode(ParseContext &context, Expression *expr) {
	if (!expr) {
		return nullptr;
	}

	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);

	switch (expr->kind) {
	case Expression::Kind::Literal: {
		if (auto *intVal = std::get_if<int64_t>(&expr->literalValue)) {
			return builder.getInt64(*intVal);
		} else if (auto *doubleVal = std::get_if<double>(&expr->literalValue)) {
			// For now, convert double to int64
			return builder.getInt64(static_cast<int64_t>(*doubleVal));
		} else if (auto *strVal = std::get_if<std::string>(&expr->literalValue)) {
			// String literals - for now just return 0
			// TODO: Handle strings properly
			(void)strVal;
			return builder.getInt64(0);
		}
		return builder.getInt64(0);
	}

	case Expression::Kind::Variable: {
		if (!expr->variable) {
			return nullptr;
		}
		std::string varName = expr->variable->name;

		// First check pattern bindings (function parameters)
		auto bindingIt = patternBindings.find(varName);
		if (bindingIt != patternBindings.end()) {
			// Pattern parameter - it's a pointer, load the value
			return builder.CreateLoad(getValueType(context), bindingIt->second, varName + "_val");
		}

		// Check local variables
		auto varIt = context.llvmVariables.find(varName);
		if (varIt != context.llvmVariables.end()) {
			return builder.CreateLoad(getValueType(context), varIt->second, varName + "_val");
		}

		// Variable not found - error
		context.diagnostics.push_back(Diagnostic(Diagnostic::Level::Error, "Unknown variable: " + varName, expr->range));
		return nullptr;
	}

	case Expression::Kind::PatternCall: {
		if (!expr->patternMatch || !expr->patternMatch->matchedEndNode) {
			return nullptr;
		}

		PatternDefinition *matchedDef = expr->patternMatch->matchedEndNode->matchingDefinition;
		Section *matchedSection = matchedDef->section;
		if (!matchedSection) {
			return nullptr;
		}

		// Find the function for this pattern
		auto funcIt = sectionFunctions.find(matchedSection);
		if (funcIt == sectionFunctions.end()) {
			context.diagnostics.push_back(Diagnostic(Diagnostic::Level::Error, "No function generated for pattern", expr->range)
			);
			return nullptr;
		}

		llvm::Function *func = funcIt->second;

		// Sort arguments by position (they may have been added in different order during parsing/expansion)
		std::vector<Expression *> sortedArgs = expr->arguments;
		std::sort(sortedArgs.begin(), sortedArgs.end(), [](Expression *a, Expression *b) {
			return a->range.start() < b->range.start();
		});

		// Walk through nodesPassed to find argument nodes in order
		// Each argument node corresponds to the next argument expression (by position order)
		std::vector<llvm::Value *> args;
		size_t argIndex = 0;

		for (PatternTreeNode *node : expr->patternMatch->nodesPassed) {
			// Check if this is an argument node (has parameter name for this definition)
			auto paramIt = node->parameterNames.find(matchedDef);
			if (paramIt != node->parameterNames.end() && argIndex < sortedArgs.size()) {
				Expression *argExpr = sortedArgs[argIndex++];
				llvm::Value *argVal = generateExpressionCode(context, argExpr);
				if (argVal) {
					// Create a temporary alloca to pass by reference
					llvm::AllocaInst *tempAlloca = builder.CreateAlloca(getValueType(context), nullptr, "tmp");
					builder.CreateStore(argVal, tempAlloca);
					args.push_back(tempAlloca);
				}
			}
		}

		// Call the pattern function
		return builder.CreateCall(func, args);
	}

	case Expression::Kind::IntrinsicCall: {
		// Generate code for intrinsic arguments (skip first arg which is the intrinsic name)
		std::vector<llvm::Value *> args;

		// Special handling for store: first arg is a pointer, second is a value
		if (expr->intrinsicName == "store" && expr->arguments.size() >= 3) {
			// args[0] is the intrinsic name, args[1] is var, args[2] is val
			llvm::Value *ptr = getVariablePointer(context, expr->arguments[1]);
			llvm::Value *val = generateExpressionCode(context, expr->arguments[2]);
			if (ptr)
				args.push_back(ptr);
			if (val)
				args.push_back(val);
		} else {
			// Normal handling: generate all args as values
			for (size_t i = 1; i < expr->arguments.size(); ++i) {
				llvm::Value *argVal = generateExpressionCode(context, expr->arguments[i]);
				if (argVal) {
					args.push_back(argVal);
				}
			}
		}

		return generateIntrinsicCode(context, expr->intrinsicName, args);
	}

	case Expression::Kind::Pending:
		// Should not happen after resolution
		context.diagnostics.push_back(Diagnostic(Diagnostic::Level::Error, "Unresolved pending expression", expr->range));
		return nullptr;
	}

	return nullptr;
}

// Generate code for an intrinsic call
static llvm::Value *
generateIntrinsicCode(ParseContext &context, const std::string &name, const std::vector<llvm::Value *> &args) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);

	if (name == "store") {
		// store(var, val) - first arg is the variable (already a pointer from pattern binding),
		// second is the value
		if (args.size() >= 2) {
			// In pattern context, first arg should be a pointer (pattern parameter)
			// We need to store the second arg into the first
			llvm::Value *ptr = args[0];
			llvm::Value *val = args[1];

			// Check if ptr is actually a pointer
			if (ptr->getType()->isPointerTy()) {
				builder.CreateStore(val, ptr);
			} else {
				// If it's a value, we can't store to it - error
				context.diagnostics.push_back(Diagnostic(Diagnostic::Level::Error, "Cannot store to non-pointer value", Range())
				);
			}
		}
		return nullptr; // store doesn't return a value
	}

	if (name == "add") {
		// add(left, right)
		if (args.size() >= 2) {
			return builder.CreateAdd(args[0], args[1], "addtmp");
		}
		return builder.getInt64(0);
	}

	if (name == "return") {
		// return(value) - this is handled by the expression pattern return
		// The value is passed back through the expression
		if (args.size() >= 1) {
			return args[0];
		}
		return builder.getInt64(0);
	}

	if (name == "print") {
		// Skip print for now - will be handled via library function later
		return nullptr;
	}

	// Unknown intrinsic
	context.diagnostics.push_back(Diagnostic(Diagnostic::Level::Error, "Unknown intrinsic: " + name, Range()));
	return nullptr;
}

// Generate code for a section (process pattern references)
static bool generateSectionCode(ParseContext &context, Section *section) {
	// Allocate variables for this section
	allocateSectionVariables(context, section);

	// Generate code for each pattern reference in this section
	for (PatternReference *reference : section->patternReferences) {
		if (reference->match) {
			// Find the corresponding code line to get the expression
			CodeLine *line = reference->range.line;
			if (line && line->expression) {
				generateExpressionCode(context, line->expression);
			}
		}
	}

	return true;
}

bool generateCode(ParseContext &context) {
	// Clear static state
	sectionFunctions.clear();
	patternBindings.clear();

	// Initialize LLVM state
	context.llvmContext = new llvm::LLVMContext();
	context.llvmModule = new llvm::Module("3bx_module", *context.llvmContext);
	context.llvmBuilder = new llvm::IRBuilder<>(*context.llvmContext);

	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);

	// First pass: Generate functions for all pattern definitions
	if (!generatePatternFunctions(context, context.mainSection)) {
		return false;
	}

	// Create main function
	llvm::FunctionType *mainType = llvm::FunctionType::get(builder.getInt32Ty(), false);
	llvm::Function *mainFunc = llvm::Function::Create(mainType, llvm::Function::ExternalLinkage, "main", context.llvmModule);

	llvm::BasicBlock *entry = llvm::BasicBlock::Create(*context.llvmContext, "entry", mainFunc);
	builder.SetInsertPoint(entry);

	// Second pass: Generate code for the main section
	if (!generateSectionCode(context, context.mainSection)) {
		return false;
	}

	// Return 0 from main
	builder.CreateRet(builder.getInt32(0));

	// Verify the module
	std::string error;
	llvm::raw_string_ostream errorStream(error);
	if (llvm::verifyModule(*context.llvmModule, &errorStream)) {
		context.diagnostics.push_back(Diagnostic(Diagnostic::Level::Error, "LLVM verification failed: " + error, Range()));
		return false;
	}

	// Output based on options
	if (context.options.emitLLVM) {
		std::string outputPath = context.options.outputPath;
		if (outputPath.empty()) {
			outputPath = context.options.inputPath + ".ll";
		}
		std::error_code ec;
		llvm::raw_fd_ostream out(outputPath, ec);
		if (ec) {
			context.diagnostics.push_back(
				Diagnostic(Diagnostic::Level::Error, "Failed to open output file: " + ec.message(), Range())
			);
			return false;
		}
		context.llvmModule->print(out, nullptr);
	} else {
		// TODO: Compile to executable
		context.diagnostics.push_back(
			Diagnostic(Diagnostic::Level::Error, "Compiling to executable not yet implemented", Range())
		);
		return false;
	}

	return true;
}
