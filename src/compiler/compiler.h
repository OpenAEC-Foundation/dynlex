#include "parseContext.h"
#include <string>

struct Variable;
struct Section;

bool compile(const std::string &path, ParseContext &context);
bool importSourceFile(const std::string &path, ParseContext &context);
bool analyzeSections(ParseContext &context);
bool resolvePatterns(ParseContext &context);
bool inferTypes(ParseContext &context);

// Intrinsic operator checking utilities
bool isArithmeticOperator(const std::string &name);
bool isPointerArithmeticOperator(const std::string &name);
bool isComparisonOperator(const std::string &name);

// Helper utilities - sortArgumentsByPosition is now inline in expression.h
