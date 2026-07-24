#pragma once
#include "expression.h"
#include "parseContext.h"
#include "type.h"
#include "llvm/Support/Alignment.h"
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

struct CodegenResult {
	llvm::Value *value = nullptr;
	bool succeeded = true;

	CodegenResult(llvm::Value *value = nullptr) : value(value) {}

	static CodegenResult failure() {
		CodegenResult result;
		result.succeeded = false;
		return result;
	}

	explicit operator bool() const { return succeeded; }
};

enum class LValueAddressStatus { Addressable, NotAddressable, Failed };

struct LValueAddressResult {
	llvm::Value *address = nullptr;
	LValueAddressStatus status = LValueAddressStatus::NotAddressable;
};

// Shared codegen utilities (codegenTypes.cpp)
llvm::Type *getLLVMType(ParseContext &context, DataType type);
llvm::Align getLLVMABIAlignment(ParseContext &context, DataType type);
unsigned getClassFieldLLVMIndex(ParseContext &context, const DataType &classType, int fieldIndex);
llvm::Value *
createClassFieldPointer(ParseContext &context, const DataType &classType, int fieldIndex, llvm::Value *classPointer);
llvm::Value *getVectorLaneIndexValue(ParseContext &context, unsigned index);
llvm::Value *convertConditionToBool(ParseContext &context, llvm::Value *condValue, DataType condType, const std::string &name);
Expression *resolveVariableBinding(ParseContext &context, Expression *expr);
void resolveThroughFlexLayers(ParseContext &context, Expression *&expr);
DataType finalizedExpressionType(ParseContext &context, Expression *expr);
PatternDefinition *finalizedPatternDefinition(ParseContext &context, Expression *expr);
llvm::AllocaInst *createEntryAlloca(ParseContext &context, const std::string &name, DataType type);
std::string getPatternFunctionName(Section *section);
void allocateSectionVariables(ParseContext &context, Section *section, InstantiatedSectionBody *body = nullptr);
LValueAddressResult generateLValueAddress(ParseContext &context, Expression *expr);
llvm::Value *ensureType(ParseContext &context, llvm::Value *val, DataType fromType, DataType toType);

// Managed-value lifecycle (managedLifecycle.cpp)
bool managedExpressionResultIsOwned(ParseContext &context, Expression *expression);
bool retainManagedValue(ParseContext &context, const DataType &type, llvm::Value *value);
bool releaseManagedValue(ParseContext &context, const DataType &type, llvm::Value *value);
void registerManagedStorage(ParseContext &context, llvm::Value *address, const DataType &type, Section *ownerSection);
void registerManagedGlobalStorage(ParseContext &context, llvm::Value *address, const DataType &type);
void initializeManagedStorage(ParseContext &context, llvm::Value *address, const DataType &type, llvm::Value *ownedValue);
bool storeManagedValue(ParseContext &context, llvm::Value *address, const DataType &type, llvm::Value *ownedValue);
bool releaseManagedTemporaryStorage(ParseContext &context, llvm::Value *address);
bool releaseManagedStorageForSection(ParseContext &context, Section *ownerSection);
bool releaseManagedStorageForReturn(ParseContext &context);
bool releaseAllManagedStorage(ParseContext &context);

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
bool generateSectionCode(
	ParseContext &context, Section *section, InstantiatedSectionBody *body = nullptr, llvm::Value **generatedValue = nullptr
);
CodegenResult generateExpressionCode(ParseContext &context, Expression *expr);
ParseContext::SectionFlexBodyFrame &activeSectionFlexBodyFrame(ParseContext &context);
bool emitSectionFlexCallerBody(
	ParseContext &context, ParseContext::SectionFlexBodyFrame &frame, Section *executionSection, bool finalizeControlFlow = true
);
bool generateSpecializedFunction(
	ParseContext &context, Section *section, const std::vector<std::pair<std::string, Expression *>> &paramBindings,
	Instantiation &instantiation
);
bool ensureCallableFunctionGenerated(
	ParseContext &context, PatternDefinition *definition, bool requireExternalLinkage, llvm::Function *&generatedFunction
);

// Intrinsic code generation (codegenIntrinsics.cpp)
CodegenResult generateIntrinsicCode(
	ParseContext &context, Expression *callExpr, const std::string &name, const std::vector<Expression *> &args,
	DataType resultType
);
std::string getStringLiteral(Expression *expr);
