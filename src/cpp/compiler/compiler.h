#pragma once

#include "parseContext.h"
#include <string>
#include <string_view>
#include <vector>

struct Variable;
struct Section;
struct Instantiation;
struct PatternDefinition;
struct InferenceContext;

bool compile(const std::string &path, ParseContext &context);
bool importSourceFile(const std::string &path, ParseContext &context);
bool analyzeSections(ParseContext &context);
bool resolvePatterns(ParseContext &context);
bool validate(ParseContext &context);
bool inferTypes(ParseContext &context);
bool resolveTypeConstraintExpression(
	ParseContext &context, Section *section, Range sourceRange, std::string_view typeConstraintExpression, DataType &outTypeRef
);
bool ensureSectionInstantiationInferred(
	ParseContext &context, Section *section, PatternDefinition *definition, const std::vector<std::string> &parameterNames,
	const BindingFrameStack &callerBindingFrameStack, const std::vector<DataType> &argTypes,
	const Instantiation *callerInstantiation = nullptr, InferenceContext *callerContext = nullptr
);
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
std::vector<PatternDefinition *>
findDefinitionsBySignature(ParseContext &context, SectionType sectionType, std::string_view signature);
std::vector<PatternDefinition *> findCallableFunctionDefinitionsBySignature(ParseContext &context, std::string_view signature);
PatternDefinition *findDefinitionBySignature(ParseContext &context, SectionType sectionType, std::string_view signature);

// Select the best overload from multiple definitions at the same trie endpoint.
// argTypes: the deduced types of the call-site arguments (in nodesPassed order).
// Returns the best-matching definition, preferring type-constrained overloads over unconstrained ones.
PatternDefinition *selectOverload(
	const std::vector<PatternDefinition *> &definitions, const std::vector<Expression *> &sortedArgs,
	const std::vector<PatternTreeNode *> &nodesPassed, const std::vector<DataType> &argTypes
);

void appendPatternCallBindings(Expression *expr, PatternDefinition *definition, BindingMap &bindings);
