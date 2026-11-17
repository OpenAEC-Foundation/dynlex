#pragma once

struct ParseContext;

// Load language defaults independently of a project's possibly unfinished config.
bool initializeBuiltinSyntaxConfig(ParseContext &context);
