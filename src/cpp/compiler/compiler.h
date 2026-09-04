#pragma once

#include "parseContext.h"
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

struct Variable;
struct Section;
struct Instantiation;
struct PatternDefinition;
struct InferenceContext;

bool compile(const std::string &path, ParseContext &context);
// resolutionRoot is the directory the file's path was resolved under; the
// file's own imports try that root before the working directory.
bool importSourceFile(const std::string &path, ParseContext &context, const std::string &resolutionRoot = "");
bool analyzeSections(ParseContext &context);
bool resolvePatterns(ParseContext &context);
bool validate(ParseContext &context);
bool inferTypes(ParseContext &context);
bool validatePatternDefinitionConflicts(ParseContext &context);
Expression *createTypeConstraintExpression(ParseContext &context, Section *section, Range sourceRange);
void destroyTypeConstraintExpression(Expression *expression);
bool ensureSectionInstantiationInferred(
	ParseContext &context, Section *section, PatternDefinition *definition,
	const std::vector<std::pair<std::string, Expression *>> &paramBindings, const std::vector<DataType> &argTypes,
	const std::unordered_set<std::string> &explicitCompileTimeParameters, const Instantiation *callerInstantiation = nullptr,
	InferenceContext *callerContext = nullptr
);
std::string renderPurityReport(ParseContext &context);
bool isInternalSourcePath(std::string_view path);
void expandExpression(Expression *expr, Section *section);

inline bool expandPendingTypeReferenceExpression(Expression *expr, Section *section) {
	if (!expr || expr->kind != Expression::Kind::Pending || !section)
		return false;
	expandExpression(expr, section);
	return true;
}

// Intrinsic operator checking utilities
bool isArithmeticOperator(const std::string &name);
bool isPointerArithmeticOperator(const std::string &name);
bool isComparisonOperator(const std::string &name);
bool isMathFunction(const std::string &name);
std::vector<PatternDefinition *> findDefinitionsBySignature(
	ParseContext &context, SectionType sectionType, std::string_view signature, const lsp::SourceFile *sourceFile
);
struct CallableFunctionMatch {
	PatternDefinition *definition{};
	size_t pathIndex{};
};

struct CallableFunctionParameter {
	std::string name;
	DataType type;
	bool requiresCompileTimeValue = false;
};

std::vector<CallableFunctionMatch>
findCallableFunctionsBySignature(ParseContext &context, std::string_view signature, const lsp::SourceFile *sourceFile);
void collectCallableFunctionParameters(
	const CallableFunctionMatch &match, std::vector<CallableFunctionParameter> &outParameters
);
PatternDefinition *findDefinitionBySignature(
	ParseContext &context, SectionType sectionType, std::string_view signature, const lsp::SourceFile *sourceFile
);

// Select the best definition/path occurrence at a trie endpoint. Every authored
// path compatible with nodesPassed participates independently because
// structurally identical choice alternatives can carry different constraints.
// argTypes: the deduced types of the call-site arguments (in nodesPassed order).
// Prefers type-constrained overloads over unconstrained ones.
struct PatternOverloadSelection {
	PatternDefinition *definition{};
	size_t pathIndex{};
	bool ambiguous = false;

	explicit operator bool() const { return definition != nullptr; }
};

struct ResolvedPatternConstraint {
	TypeConstraint constraint;
	bool requiresCompileTimeValue = false;
	bool acceptsUnresolvedType = false;
	bool acceptsNothing = false;

	TypeConstraint effectiveConstraint() const {
		TypeConstraint result = constraint;
		result.requiresCompileTimeValue = result.requiresCompileTimeValue || requiresCompileTimeValue;
		return result;
	}

	bool accepts(const DataType &argumentType, bool compileTimeKnown) const {
		if (argumentType.kind == DataType::Kind::Void && !acceptsNothing)
			return false;
		return effectiveConstraint().accepts(argumentType, compileTimeKnown);
	}
};

using PatternConstraintResolver = std::function<std::optional<ResolvedPatternConstraint>(PatternDefinition *, size_t, size_t)>;

std::optional<ResolvedPatternConstraint>
resolveInitialPatternConstraint(PatternDefinition *definition, size_t pathIndex, size_t argumentIndex);
std::optional<ResolvedPatternConstraint> resolveCompiledPatternConstraint(
	PatternDefinition *definition, size_t pathIndex, size_t argumentIndex, const std::vector<DataType> &argumentTypes,
	const std::vector<CompileTimeValue> &argumentValues
);
PatternOverloadSelection selectOverload(
	const std::vector<PatternDefinition *> &definitions, const std::vector<Expression *> &sortedArgs,
	const std::vector<PatternTreeNode *> &nodesPassed, const std::vector<DataType> &argTypes,
	const std::vector<bool> &argCompileTimeKnown, const PatternConstraintResolver &resolveConstraint
);
const DefinitionPatternElement *
matchedPatternParameterElement(PatternDefinition *definition, std::string_view parameterName, size_t startPos);
std::unordered_set<std::string> collectExplicitCompileTimeParameters(
	PatternDefinition *definition, const std::vector<std::pair<std::string, Expression *>> &paramBindings, size_t pathIndex,
	const std::vector<DataType> &argTypes, const std::vector<TypeConstraint> &argumentConstraints
);

void appendPatternCallBindings(Expression *expr, PatternDefinition *definition, BindingMap &bindings);
