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
void resolveThroughFlexLayers(ParseContext &context, Expression *&expr);
DataType finalizedExpressionType(ParseContext &context, Expression *expr);
PatternDefinition *finalizedPatternDefinition(ParseContext &context, Expression *expr);
llvm::AllocaInst *createEntryAlloca(ParseContext &context, const std::string &name, DataType type);
std::string getPatternFunctionName(Section *section);
void allocateSectionVariables(ParseContext &context, Section *section, InstantiatedSectionBody *body = nullptr);
llvm::Value *getVariablePointer(ParseContext &context, Expression *expr);
llvm::Value *ensureType(ParseContext &context, llvm::Value *val, DataType fromType, DataType toType);

// FlexScopeGuard: RAII guard that pops to caller's flex binding scope, restores on destruction.
struct FlexScopeGuard {
	ParseContext &context;
	BindingFrameStack savedBindingFrames;
	bool active = false;

	FlexScopeGuard(ParseContext &ctx) : context(ctx) {}
	FlexScopeGuard(const FlexScopeGuard &) = delete;
	FlexScopeGuard &operator=(const FlexScopeGuard &) = delete;

	void popToCallerScope();
	~FlexScopeGuard();
};

// Debug info helpers (codegenTypes.cpp)
llvm::DIType *getDIType(ParseContext &context, DataType type);
llvm::DIFile *getOrCreateDIFile(ParseContext &context, lsp::SourceFile *sourceFile);

// Function/section code generation (codegen.cpp)
bool generateSectionCode(ParseContext &context, Section *section, InstantiatedSectionBody *body = nullptr);
llvm::Value *generateExpressionCode(ParseContext &context, Expression *expr);
void emitFlexBodySection(
	ParseContext &context, Section *bodySection, InstantiatedSectionBody *body = nullptr, bool finalizeControlFlow = true
);
Instantiation *generateSpecializedFunction(
	ParseContext &context, Section *section, const std::vector<std::pair<std::string, Expression *>> &paramBindings,
	Instantiation &instantiation
);
llvm::Function *
ensureCallableFunctionGenerated(ParseContext &context, PatternDefinition *definition, bool requireExternalLinkage);

// Intrinsic code generation (codegenIntrinsics.cpp)
llvm::Value *generateIntrinsicCode(
	ParseContext &context, Expression *callExpr, const std::string &name, const std::vector<Expression *> &args,
	DataType resultType
);
std::string getStringLiteral(Expression *expr);
