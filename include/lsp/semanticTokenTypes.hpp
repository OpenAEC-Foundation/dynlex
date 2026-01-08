#pragma once

#include <string>
#include <vector>

namespace tbx {

enum class SemanticTokenType {
  Namespace,
  Type,
  Class,
  Enum,
  Interface,
  Struct,
  TypeParameter,
  Parameter,
  Variable,
  Property,
  EnumMember,
  Event,
  Function,
  Method,
  Macro,
  Keyword,
  Modifier,
  Comment,
  String,
  Number,
  Regexp,
  Operator,
  Decorator,
  // Custom types for 3BX
  Pattern,
  Effect,
  Expression,
  Section
};

inline const std::vector<std::string> &getSemanticTokenTypes() {
  static const std::vector<std::string> types = {
      "namespace",  "type",          "class",     "enum",     "interface",
      "struct",     "typeParameter", "parameter", "variable", "property",
      "enumMember", "event",         "function",  "method",   "macro",
      "keyword",    "modifier",      "comment",   "string",   "number",
      "regexp",     "operator",      "decorator", "function", "function",
      "function",   "function"};
  return types;
}

} // namespace tbx
