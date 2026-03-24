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
struct PatternDefinition;

// Shared codegen utilities (codegenTypes.cpp)
llvm::Type *getLLVMType(ParseContext &context, DataType type);
llvm::Value *getVectorLaneIndexValue(ParseContext &context, unsigned index);
llvm::Value *convertConditionToBool(ParseContext &context, llvm::Value *condValue, DataType condType, const std::string &name);
Expression *resolveVariableBinding(ParseContext &context, Expression *expr);
void resolveThroughMacroLayers(ParseContext &context, Expression *&expr);
DataType getEffectiveType(ParseContext &context, Expression *expr);
PatternDefinition *selectCodegenOverload(ParseContext &context, Expression *expr);
llvm::AllocaInst *createEntryAlloca(ParseContext &context, const std::string &name, DataType type);
std::string getPatternFunctionName(Section *section);
void allocateSectionVariables(ParseContext &context, Section *section);
llvm::Value *getVariablePointer(ParseContext &context, Expression *expr);
llvm::Value *ensureType(ParseContext &context, llvm::Value *val, DataType fromType, DataType toType);

// MacroScopeGuard: RAII guard that pops to caller's macro binding scope, restores on destruction.
struct MacroScopeGuard {
	ParseContext &context;
	BindingFrameStack savedBindingFrames;
	bool active = false;

	MacroScopeGuard(ParseContext &ctx) : context(ctx) {}
	MacroScopeGuard(const MacroScopeGuard &) = delete;
	MacroScopeGuard &operator=(const MacroScopeGuard &) = delete;

	void popToCallerScope();
	~MacroScopeGuard();
};

// Debug info helpers (codegenTypes.cpp)
llvm::DIType *getDIType(ParseContext &context, DataType type);
llvm::DIFile *getOrCreateDIFile(ParseContext &context, lsp::SourceFile *sourceFile);

// Function/section code generation (codegen.cpp)
bool generateSectionCode(ParseContext &context, Section *section);
llvm::Value *generateExpressionCode(ParseContext &context, Expression *expr);
Instantiation *generateSpecializedFunction(
	ParseContext &context, Section *section, const std::vector<std::pair<std::string, Expression *>> &paramBindings,
	const std::vector<DataType> &argTypes
);
llvm::Function *
ensureCallableFunctionGenerated(ParseContext &context, PatternDefinition *definition, bool requireExternalLinkage);

// Intrinsic code generation (codegenIntrinsics.cpp)
llvm::Value *generateIntrinsicCode(
	ParseContext &context, const std::string &name, const std::vector<Expression *> &args, DataType resultType
);
std::string getStringLiteral(Expression *expr);
