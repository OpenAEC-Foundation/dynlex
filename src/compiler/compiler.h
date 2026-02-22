#include "parseContext.h"
#include <string>

struct Variable;
struct Section;

bool compile(const std::string &path, ParseContext &context);
bool importSourceFile(const std::string &path, ParseContext &context);
bool analyzeSections(ParseContext &context);
bool resolvePatterns(ParseContext &context);
bool validate(ParseContext &context);
bool inferTypes(ParseContext &context);
void runInference(ParseContext &context);
bool expressionTypesValid(Expression *expr);
void expandExpression(Expression *expr, Section *section);

// Intrinsic operator checking utilities
bool isArithmeticOperator(const std::string &name);
bool isPointerArithmeticOperator(const std::string &name);
bool isComparisonOperator(const std::string &name);
bool isMathFunction(const std::string &name);

// Select the best overload from multiple definitions at the same trie endpoint.
// argTypes: the deduced types of the call-site arguments (in nodesPassed order).
// Returns the best-matching definition, preferring type-constrained overloads over unconstrained ones.
PatternDefinition *selectOverload(
	const std::vector<PatternDefinition *> &definitions, const std::vector<Expression *> &sortedArgs,
	const std::vector<PatternTreeNode *> &nodesPassed, const std::vector<DataType> &argTypes
);
