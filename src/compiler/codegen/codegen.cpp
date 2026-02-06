#include "codegen.h"
#include "expression.h"
#include "native.h"
#include "patternDefinition.h"
#include "patternReference.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Host.h"
#include <algorithm>
#include <unordered_map>

// Forward declarations
static bool generatePatternFunctions(ParseContext &context, Section *section);
static bool generateSectionCode(ParseContext &context, Section *section);
static llvm::Value *generateExpressionCode(ParseContext &context, Expression *expr);
static llvm::Value *
generateIntrinsicCode(ParseContext &context, const std::string &name, const std::vector<Expression *> &args);
static llvm::Value *generateLibraryCall(
	ParseContext &context, const std::string &library, const std::string &funcName, const std::string &formatStr,
	const std::vector<llvm::Value *> &args
);

// Get the LLVM type for values (i64 for now)
static llvm::Type *getValueType(ParseContext &context) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	return builder.getInt64Ty();
}

// Get the LLVM type for value pointers (i64*)
static llvm::Type *getValuePtrType(ParseContext &context) { return llvm::PointerType::getUnqual(getValueType(context)); }

// Create an alloca at function entry (avoids stack growth in loops)
static llvm::AllocaInst *createEntryAlloca(ParseContext &context, const std::string &name) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	llvm::Function *func = builder.GetInsertBlock()->getParent();
	llvm::IRBuilder<> entryBuilder(&func->getEntryBlock(), func->getEntryBlock().begin());
	llvm::AllocaInst *alloca = entryBuilder.CreateAlloca(getValueType(context), nullptr, name);
	alloca->setAlignment(llvm::Align(8));
	return alloca;
}

// Generate a unique function name for a pattern
static std::string getPatternFunctionName(Section *section) {
	// Use section address as unique identifier for now
	// TODO: Generate readable names from pattern text

	// effect patterns have other names than effect patterns so we don't have to add extra info to it
	std::string name = (std::string)section->patternDefinitions.front()->range.subString;
	// convert string to alphanumeric
	for (char &c : name) {
		if (!isalnum(c) && c != '_') {

			c = (c == ' ') ? '_' : (c % 10 + '0');
		}
	}
	// add section address for now
	name += std::to_string(reinterpret_cast<uintptr_t>(section));
	return name;
}

// Get variable names from a pattern definition (only actual Variable parameters, not literal words)
static std::vector<std::string> getPatternArgumentNames(Section *section) {
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
	std::vector<std::string> varNames = getPatternArgumentNames(section);

	// Build function type: all parameters are i64* (pass by reference)
	std::vector<llvm::Type *> paramTypes(varNames.size(), getValuePtrType(context));

	// Return type: void for effects, i64 for expressions
	llvm::Type *returnType = (section->type == SectionType::Effect) ? builder.getVoidTy() : getValueType(context);

	llvm::FunctionType *funcType = llvm::FunctionType::get(returnType, paramTypes, false);

	// Create the function
	std::string funcName = getPatternFunctionName(section);
	llvm::Function *func = llvm::Function::Create(funcType, llvm::Function::InternalLinkage, funcName, context.llvmModule);

	// Name the parameters
	size_t argumentIndex = 0;
	for (auto &arg : func->args()) {
		arg.setName(varNames[argumentIndex++]);
	}

	// Create entry block
	llvm::BasicBlock *entry = llvm::BasicBlock::Create(*context.llvmContext, "entry", func);

	// Save current insert point
	llvm::BasicBlock *savedBlock = builder.GetInsertBlock();
	llvm::BasicBlock::iterator savedPoint = builder.GetInsertPoint();

	// Set insert point to new function
	builder.SetInsertPoint(entry);

	// Set up pattern bindings - map variable names to function parameters
	context.patternBindings.clear();
	argumentIndex = 0;
	for (auto &arg : func->args()) {
		context.patternBindings[varNames[argumentIndex++]] = &arg;
	}

	for (Section *child : section->children) {
		// Generate code for each line in the child section
		for (CodeLine *line : child->codeLines) {
			if (line->expression) {
				generateExpressionCode(context, line->expression);
			}
		}
	}

	// Add return statement
	if (section->type == SectionType::Effect) {
		builder.CreateRetVoid();
	} // if it's an expression, @intrinsic("return") should handle this. todo: add error when not returning a value

	// Clear pattern bindings
	context.patternBindings.clear();

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
		section->generatedFunction = func;
	}

	// Recurse into children
	for (Section *child : section->children) {
		if (!generatePatternFunctions(context, child)) {
			return false;
		}
	}

	return true;
}

// Allocate all variables for a section at its start
static void allocateSectionVariables(ParseContext &context, Section *section) {
	for (auto &[name, varDef] : section->variableDefinitions) {
		varDef->alloca = createEntryAlloca(context, name);
	}
}

// Get the pointer for a variable expression (for store operations)
static llvm::Value *getVariablePointer(ParseContext &context, Expression *expr) {
	if (!expr) {
		return nullptr;
	}

	// For variable expressions, check bindings and local variables
	if (expr->kind == Expression::Kind::Variable && expr->variable) {
		std::string varName = expr->variable->name;

		// First check macro expression bindings - substitute and recurse
		auto macroIt = context.macroExpressionBindings.find(varName);
		if (macroIt != context.macroExpressionBindings.end()) {
			return getVariablePointer(context, macroIt->second);
		}

		// Check pattern bindings (function parameters - already pointers)
		auto bindingIt = context.patternBindings.find(varName);
		if (bindingIt != context.patternBindings.end()) {
			return bindingIt->second;
		}

		// Check local variables - get alloca from the variable's definition
		VariableReference *varRef = expr->variable;
		VariableReference *definition = varRef->definition ? varRef->definition : varRef;
		if (definition->alloca) {
			return definition->alloca;
		}
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

		// First check macro expression bindings - substitute and generate
		auto macroIt = context.macroExpressionBindings.find(varName);
		if (macroIt != context.macroExpressionBindings.end()) {
			return generateExpressionCode(context, macroIt->second);
		}

		// Check pattern bindings (function parameters)
		auto bindingIt = context.patternBindings.find(varName);
		if (bindingIt != context.patternBindings.end()) {
			// Pattern parameter - it's a pointer, load the value
			return builder.CreateAlignedLoad(getValueType(context), bindingIt->second, llvm::Align(8), varName + "_val");
		}

		// Check local variables - get alloca from the variable's definition
		VariableReference *varRef = expr->variable;
		VariableReference *definition = varRef->definition ? varRef->definition : varRef;
		if (definition->alloca) {
			return builder.CreateAlignedLoad(getValueType(context), definition->alloca, llvm::Align(8), varName + "_val");
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

		// Sort arguments by position (they may have been added in different order during parsing/expansion)
		std::vector<Expression *> sortedArgs = expr->arguments;
		std::sort(sortedArgs.begin(), sortedArgs.end(), [](Expression *a, Expression *b) {
			return a->range.start() < b->range.start();
		});

		// Build mapping from parameter names to argument expressions
		std::vector<std::pair<std::string, Expression *>> paramBindings;
		size_t argIndex = 0;
		for (PatternTreeNode *node : expr->patternMatch->nodesPassed) {
			auto paramIt = node->parameterNames.find(matchedDef);
			if (paramIt != node->parameterNames.end() && argIndex < sortedArgs.size()) {
				paramBindings.push_back({paramIt->second, sortedArgs[argIndex++]});
			}
		}

		if (matchedSection->isMacro) {
			// Macro: inline the replacement body with expression substitution
			auto savedMacroBindings = context.macroExpressionBindings;
			Section *savedBodySection = context.currentBodySection;

			// Bind parameter names to argument expressions
			for (const auto &[paramName, argExpr] : paramBindings) {
				context.macroExpressionBindings[paramName] = argExpr;
			}

			// Set current body section for intrinsics that need it (loop while, if, etc.)
			Section *bodySection = expr->range.line ? expr->range.line->sectionOpening : nullptr;
			context.currentBodySection = bodySection;

			// Generate code for the replacement body (may set up control flow on bodySection)
			llvm::Value *result = nullptr;
			for (Section *child : matchedSection->children) {
				for (CodeLine *line : child->codeLines) {
					if (line->expression) {
						result = generateExpressionCode(context, line->expression);
					}
				}
			}

			// Generate body section if present (for constructs like while loops, if, etc.)
			if (bodySection) {
				generateSectionCode(context, bodySection);

				// Close control flow: branch to appropriate target, then continue at exit block
				if (bodySection->exitBlock) {
					if (!builder.GetInsertBlock()->getTerminator()) {
						// Loops branch back to condition, if/else branches to exit
						llvm::BasicBlock *target =
							bodySection->branchBackBlock ? bodySection->branchBackBlock : bodySection->exitBlock;
						builder.CreateBr(target);
					}
					builder.SetInsertPoint(bodySection->exitBlock);
				}
			}

			// Restore previous state
			context.macroExpressionBindings = savedMacroBindings;
			context.currentBodySection = savedBodySection;

			return result;
		}

		// Regular pattern: call the generated function
		llvm::Function *func = matchedSection->generatedFunction;
		if (!func) {
			context.diagnostics.push_back(Diagnostic(Diagnostic::Level::Error, "No function generated for pattern", expr->range)
			);
			return nullptr;
		}

		// Generate argument values and pass by reference
		// For variables, pass their pointer directly (enables mutation)
		// For expressions/literals, wrap in a temp alloca
		// Let LLVM's argpromotion pass convert to pass-by-value where applicable
		std::vector<llvm::Value *> args;
		for (const auto &[paramName, argExpr] : paramBindings) {
			llvm::Value *ptr = getVariablePointer(context, argExpr);
			if (ptr) {
				// Variable - pass pointer directly
				args.push_back(ptr);
			} else {
				// Expression/literal - evaluate and wrap in temp
				llvm::Value *argVal = generateExpressionCode(context, argExpr);
				if (argVal) {
					llvm::AllocaInst *tempAlloca = createEntryAlloca(context, "tmp");
					builder.CreateAlignedStore(argVal, tempAlloca, llvm::Align(8));
					args.push_back(tempAlloca);
				}
			}
		}

		return builder.CreateCall(func, args);
	}

	case Expression::Kind::IntrinsicCall: {
		// Pass expressions directly to intrinsic - let it decide when/how to evaluate
		// Skip first argument (intrinsic name)
		std::vector<Expression *> args(expr->arguments.begin() + 1, expr->arguments.end());
		return generateIntrinsicCode(context, expr->intrinsicName, args);
	}

	case Expression::Kind::Pending:
		// Should not happen after resolution
		context.diagnostics.push_back(Diagnostic(Diagnostic::Level::Error, "Unresolved pending expression", expr->range));
		return nullptr;
	}

	return nullptr;
}

// Helper to extract string literal from expression
static std::string getStringLiteral(Expression *expr) {
	if (expr && expr->kind == Expression::Kind::Literal) {
		if (auto *str = std::get_if<std::string>(&expr->literalValue)) {
			return *str;
		}
	}
	return "";
}

// Generate code for an intrinsic call
// Arguments are passed as unevaluated expressions - intrinsic decides when/how to evaluate
static llvm::Value *
generateIntrinsicCode(ParseContext &context, const std::string &name, const std::vector<Expression *> &args) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);

	if (name == "store") {
		// store(var, val) - first arg is variable (get pointer), second is value
		if (args.size() >= 2) {
			llvm::Value *ptr = getVariablePointer(context, args[0]);
			llvm::Value *val = generateExpressionCode(context, args[1]);
			if (ptr && val) {
				builder.CreateAlignedStore(val, ptr, llvm::Align(8));
			} else if (!ptr) {
				context.diagnostics.push_back(Diagnostic(Diagnostic::Level::Error, "Cannot store to non-variable", Range()));
			}
		}
		return nullptr;
	}

	if (name == "add") {
		if (args.size() >= 2) {
			llvm::Value *left = generateExpressionCode(context, args[0]);
			llvm::Value *right = generateExpressionCode(context, args[1]);
			return builder.CreateAdd(left, right, "add");
		}
		return builder.getInt64(0);
	}

	if (name == "subtract") {
		if (args.size() >= 2) {
			llvm::Value *left = generateExpressionCode(context, args[0]);
			llvm::Value *right = generateExpressionCode(context, args[1]);
			return builder.CreateSub(left, right, "sub");
		}
		return builder.getInt64(0);
	}

	if (name == "multiply") {
		if (args.size() >= 2) {
			llvm::Value *left = generateExpressionCode(context, args[0]);
			llvm::Value *right = generateExpressionCode(context, args[1]);
			return builder.CreateMul(left, right, "mul");
		}
		return builder.getInt64(0);
	}

	if (name == "less than") {
		if (args.size() >= 2) {
			llvm::Value *left = generateExpressionCode(context, args[0]);
			llvm::Value *right = generateExpressionCode(context, args[1]);
			llvm::Value *cmp = builder.CreateICmpSLT(left, right, "lt");
			return builder.CreateZExt(cmp, getValueType(context), "lt_ext");
		}
		return builder.getInt64(0);
	}

	if (name == "greater than") {
		if (args.size() >= 2) {
			llvm::Value *left = generateExpressionCode(context, args[0]);
			llvm::Value *right = generateExpressionCode(context, args[1]);
			llvm::Value *cmp = builder.CreateICmpSGT(left, right, "gt");
			return builder.CreateZExt(cmp, getValueType(context), "gt_ext");
		}
		return builder.getInt64(0);
	}

	if (name == "equal") {
		if (args.size() >= 2) {
			llvm::Value *left = generateExpressionCode(context, args[0]);
			llvm::Value *right = generateExpressionCode(context, args[1]);
			llvm::Value *cmp = builder.CreateICmpEQ(left, right, "eq");
			return builder.CreateZExt(cmp, getValueType(context), "eq_ext");
		}
		return builder.getInt64(0);
	}

	if (name == "not equal") {
		if (args.size() >= 2) {
			llvm::Value *left = generateExpressionCode(context, args[0]);
			llvm::Value *right = generateExpressionCode(context, args[1]);
			llvm::Value *cmp = builder.CreateICmpNE(left, right, "ne");
			return builder.CreateZExt(cmp, getValueType(context), "ne_ext");
		}
		return builder.getInt64(0);
	}

	if (name == "divide") {
		if (args.size() >= 2) {
			llvm::Value *left = generateExpressionCode(context, args[0]);
			llvm::Value *right = generateExpressionCode(context, args[1]);
			return builder.CreateSDiv(left, right, "div");
		}
		return builder.getInt64(0);
	}

	if (name == "modulo") {
		if (args.size() >= 2) {
			llvm::Value *left = generateExpressionCode(context, args[0]);
			llvm::Value *right = generateExpressionCode(context, args[1]);
			return builder.CreateSRem(left, right, "mod");
		}
		return builder.getInt64(0);
	}

	if (name == "loop while") {
		// loop while(condition) - creates loop structure, condition re-evaluated each iteration
		if (args.empty()) {
			context.diagnostics.push_back(Diagnostic(Diagnostic::Level::Error, "loop while requires a condition", Range()));
			return nullptr;
		}

		Section *bodySection = context.currentBodySection;
		if (!bodySection) {
			context.diagnostics.push_back(Diagnostic(Diagnostic::Level::Error, "loop while requires a body section", Range()));
			return nullptr;
		}

		llvm::Function *func = builder.GetInsertBlock()->getParent();

		// Create basic blocks
		llvm::BasicBlock *condBlock = llvm::BasicBlock::Create(*context.llvmContext, "while_cond", func);
		llvm::BasicBlock *bodyBlock = llvm::BasicBlock::Create(*context.llvmContext, "while_body", func);
		llvm::BasicBlock *exitBlock = llvm::BasicBlock::Create(*context.llvmContext, "while_exit", func);

		// Branch to condition block
		builder.CreateBr(condBlock);

		// Generate condition block
		builder.SetInsertPoint(condBlock);
		llvm::Value *condValue = generateExpressionCode(context, args[0]);
		llvm::Value *condBool = builder.CreateICmpNE(condValue, builder.getInt64(0), "while_cond_bool");
		builder.CreateCondBr(condBool, bodyBlock, exitBlock);

		// Set insert point to body block - body section will be generated here
		builder.SetInsertPoint(bodyBlock);

		// Store control flow info on body section for closing after body generation
		bodySection->exitBlock = exitBlock;
		bodySection->branchBackBlock = condBlock; // loops branch back to condition

		return nullptr;
	}

	if (name == "if") {
		// if(condition) - executes body once if condition is true
		if (args.empty()) {
			context.diagnostics.push_back(Diagnostic(Diagnostic::Level::Error, "if requires a condition", Range()));
			return nullptr;
		}

		Section *bodySection = context.currentBodySection;
		if (!bodySection) {
			context.diagnostics.push_back(Diagnostic(Diagnostic::Level::Error, "if requires a body section", Range()));
			return nullptr;
		}

		llvm::Function *func = builder.GetInsertBlock()->getParent();

		// Create basic blocks
		llvm::BasicBlock *thenBlock = llvm::BasicBlock::Create(*context.llvmContext, "if_then", func);
		llvm::BasicBlock *exitBlock = llvm::BasicBlock::Create(*context.llvmContext, "if_exit", func);

		// Evaluate condition and branch
		llvm::Value *condValue = generateExpressionCode(context, args[0]);
		llvm::Value *condBool = builder.CreateICmpNE(condValue, builder.getInt64(0), "if_cond");
		builder.CreateCondBr(condBool, thenBlock, exitBlock);

		// Set insert point to then block - body section will be generated here
		builder.SetInsertPoint(thenBlock);

		// Store exit block on body section (no branch back for if)
		bodySection->exitBlock = exitBlock;
		bodySection->branchBackBlock = nullptr;

		return nullptr;
	}

	if (name == "return") {
		if (args.size() >= 1) {
			llvm::Value *returnValue = generateExpressionCode(context, args[0]);
			builder.CreateRet(returnValue);
		}
		return nullptr;
	}

	if (name == "call") {
		// call(library, function, format, ...args)
		if (args.size() >= 3) {
			std::string library = getStringLiteral(args[0]);
			std::string funcName = getStringLiteral(args[1]);
			std::string formatStr = getStringLiteral(args[2]);

			std::vector<llvm::Value *> callArgs;
			for (size_t i = 3; i < args.size(); ++i) {
				llvm::Value *argVal = generateExpressionCode(context, args[i]);
				if (argVal) {
					callArgs.push_back(argVal);
				}
			}

			return generateLibraryCall(context, library, funcName, formatStr, callArgs);
		}
		return nullptr;
	}

	// Unknown intrinsic
	context.diagnostics.push_back(Diagnostic(Diagnostic::Level::Error, "Unknown intrinsic: " + name, Range()));
	return nullptr;
}

// Generate a call to an external library function
static llvm::Value *generateLibraryCall(
	ParseContext &context, const std::string &library, const std::string &funcName, const std::string &formatStr,
	const std::vector<llvm::Value *> &args
) {
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	llvm::Module *module = context.llvmModule;

	// Track library for linking (skip libc as it's implicit)
	if (!library.empty() && library != "libc") {
		context.requiredLibraries.insert(library);
	}

	// Get or create the function declaration
	llvm::Function *func = module->getFunction(funcName);
	if (!func) {
		// Create function declaration - assume varargs for printf-like functions
		llvm::FunctionType *funcType = llvm::FunctionType::get(builder.getInt32Ty(), {builder.getPtrTy()}, true /* varargs */);
		func = llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, funcName, module);
	}

	// Create format string as global constant
	std::string globalName = ".str." + funcName + "." + std::to_string(context.stringConstants.size());
	llvm::Constant *formatConstant = llvm::ConstantDataArray::getString(*context.llvmContext, formatStr, true);
	llvm::GlobalVariable *formatGlobal = new llvm::GlobalVariable(
		*module, formatConstant->getType(), true, llvm::GlobalValue::PrivateLinkage, formatConstant, globalName
	);
	context.stringConstants[formatStr] = formatGlobal;

	// Build call arguments: format string + remaining args
	std::vector<llvm::Value *> callArgs;
	callArgs.push_back(formatGlobal);
	for (llvm::Value *arg : args) {
		callArgs.push_back(arg);
	}

	return builder.CreateCall(func, callArgs);
}

// Generate code for a section (process pattern references)
static bool generateSectionCode(ParseContext &context, Section *section) {
	// Allocate variables for this section
	allocateSectionVariables(context, section);

	// Generate code for each pattern reference in this section
	for (PatternReference *reference : section->patternReferences) {
		if (reference->match) {
			// Find the corresponding code line to get the expression
			CodeLine *line = reference->range().line;
			if (line && line->expression) {
				generateExpressionCode(context, line->expression);
			}
		}
	}

	return true;
}

bool generateCode(ParseContext &context) {
	// Initialize LLVM state
	context.llvmContext = new llvm::LLVMContext();
	context.llvmModule = new llvm::Module("3bx_module", *context.llvmContext);
	context.llvmBuilder = new llvm::IRBuilder<>(*context.llvmContext);

	// Set target triple for the current platform
	context.llvmModule->setTargetTriple(llvm::sys::getDefaultTargetTriple());

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
		// Print the invalid IR for debugging
		llvm::errs() << "\n=== Invalid LLVM IR (for debugging) ===\n";
		context.llvmModule->print(llvm::errs(), nullptr);
		llvm::errs() << "=== End Invalid LLVM IR ===\n\n";

		context.diagnostics.push_back(Diagnostic(Diagnostic::Level::Error, "LLVM verification failed: " + error, Range()));
		return false;
	}

	// Run LLVM optimization passes if optimization is enabled
	if (context.options.optimizationLevel > 0) {
		llvm::LoopAnalysisManager lam;
		llvm::FunctionAnalysisManager fam;
		llvm::CGSCCAnalysisManager cgam;
		llvm::ModuleAnalysisManager mam;

		llvm::PassBuilder pb;
		pb.registerModuleAnalyses(mam);
		pb.registerCGSCCAnalyses(cgam);
		pb.registerFunctionAnalyses(fam);
		pb.registerLoopAnalyses(lam);
		pb.crossRegisterProxies(lam, fam, cgam, mam);

		// Select optimization level
		llvm::OptimizationLevel optLevel;
		switch (context.options.optimizationLevel) {
		case 1:
			optLevel = llvm::OptimizationLevel::O1;
			break;
		case 2:
			optLevel = llvm::OptimizationLevel::O2;
			break;
		case 3:
			optLevel = llvm::OptimizationLevel::O3;
			break;
		default:
			optLevel = llvm::OptimizationLevel::O1;
			break;
		}

		llvm::ModulePassManager mpm = pb.buildPerModuleDefaultPipeline(optLevel);
		mpm.run(*context.llvmModule, mam);
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
		// Compile to native executable
		if (!emitNativeExecutable(context)) {
			return false;
		}
	}

	return true;
}
