#include "parseContext.h"
#include <string>
bool compile(const std::string &path, ParseContext &context);
bool importSourceFile(const std::string &path, ParseContext &context);
bool analyzeSections(ParseContext &context);
bool resolvePatterns(ParseContext &context);