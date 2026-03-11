#include "parseContext.h"
#include <string>
#include <string_view>

struct Variable;
struct Section;
struct Instantiation;

bool compile(const std::string &path, ParseContext &context);
bool importSourceFile(const std::string &path, ParseContext &context);
bool analyzeSections(ParseContext &context);
bool resolvePatterns(ParseContext &context);
bool validate(ParseContext &context);
bool inferTypes(ParseContext &context);
bool ensureSectionInstantiationInferred(
	ParseContext &context, Section *section, const std::unordered_map<std::string, Function *> &callBindings,
	const std::vector<DataType> &argTypes, const Instantiation *callerInstantiation = nullptr
);
bool isInternalSourcePath(std::string_view path);
void expandFunction(Function *expr, Section *section);

// Intrinsic operator checking utilities
bool isArithmeticOperator(const std::string &name);
bool isPointerArithmeticOperator(const std::string &name);
bool isComparisonOperator(const std::string &name);
bool isMathFunction(const std::string &name);
PatternDefinition *findDefinitionBySignature(ParseContext &context, SectionType sectionType, std::string_view signature);

// Select the best overload from multiple definitions at the same trie endpoint.
// argTypes: the deduced types of the call-site arguments (in nodesPassed order).
// Returns the best-matching definition, preferring type-constrained overloads over unconstrained ones.
PatternDefinition *selectOverload(
	const std::vector<PatternDefinition *> &definitions, const std::vector<Function *> &sortedArgs,
	const std::vector<PatternTreeNode *> &nodesPassed, const std::vector<DataType> &argTypes
);
