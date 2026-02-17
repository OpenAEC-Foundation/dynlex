#pragma once
#include "expression.h"
#include "parseContext.h"
#include "type.h"
#include <string>
#include <vector>

namespace llvm {
class Value;
class Type;
class AllocaInst;
class DIType;
class DIFile;
} // namespace llvm

namespace lsp {
struct SourceFile;
}

// Shared codegen utilities (codegenTypes.cpp)
llvm::Type *getLLVMType(ParseContext &context, Type type);
llvm::Value *convertConditionToBool(ParseContext &context, llvm::Value *condValue, Type condType, const std::string &name);
Expression *resolveMacroBinding(ParseContext &context, Expression *expr);
Type getEffectiveType(ParseContext &context, Expression *expr);
llvm::AllocaInst *createEntryAlloca(ParseContext &context, const std::string &name, Type type);
std::string getPatternFunctionName(Section *section);
void allocateSectionVariables(ParseContext &context, Section *section);
llvm::Value *getVariablePointer(ParseContext &context, Expression *expr);
llvm::Value *ensureType(ParseContext &context, llvm::Value *val, Type fromType, Type toType);

// MacroScopeGuard: RAII guard that pops to caller's macro binding scope, restores on destruction.
struct MacroScopeGuard {
	ParseContext &context;
	std::unordered_map<std::string, Expression *> savedBindings;
	bool active = false;

	MacroScopeGuard(ParseContext &ctx) : context(ctx) {}
	MacroScopeGuard(const MacroScopeGuard &) = delete;
	MacroScopeGuard &operator=(const MacroScopeGuard &) = delete;

	void popToCallerScope();
	~MacroScopeGuard();
};

// Debug info helpers (codegenTypes.cpp)
llvm::DIType *getDIType(ParseContext &context, Type type);
llvm::DIFile *getOrCreateDIFile(ParseContext &context, lsp::SourceFile *sourceFile);

// Expression/section code generation (codegen.cpp)
bool generateSectionCode(ParseContext &context, Section *section);
llvm::Value *generateExpressionCode(ParseContext &context, Expression *expr);
void generateSpecializedFunction(
	ParseContext &context, Section *section, const std::vector<std::pair<std::string, Expression *>> &paramBindings,
	const std::vector<Type> &argTypes, Instantiation &inst
);

// Intrinsic code generation (codegenIntrinsics.cpp)
llvm::Value *
generateIntrinsicCode(ParseContext &context, const std::string &name, const std::vector<Expression *> &args, Type resultType);
std::string getStringLiteral(Expression *expr);
