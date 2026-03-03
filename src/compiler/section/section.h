#pragma once
#include "codeLine.h"
#include "compileTimeInfo.h"
#include "patternDefinition.h"
#include "patternReference.h"
#include "sectionType.h"
#include "stringHierarchy.h"
#include "type.h"
#include "variableReference.h"
#include <list>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace llvm {
class Function;
class BasicBlock;
} // namespace llvm

struct ParseContext;
struct Variable;
struct Function;
// Per-instantiation state for monomorphized functions.
// Each unique combination of argument types produces a separate instantiation.
struct Instantiation {
	DataType returnType{DataType::Kind::Any};
	std::vector<DataType> parameterTypes;
	std::unordered_map<std::string, CompileTimeValue> constantParameterValues;
	std::unordered_set<std::string> requiredCompileTimeParameters;
	llvm::Function *llvmFunction = nullptr;
	bool inferring = false;
	bool valid = true;
};

struct Section {
	inline Section(SectionType type, Section *parent = {}) : type(type), parent(parent) {
		if (parent) {
			parent->children.push_back(this);
		}
	}
	SectionType type;
	Section *parent{};
	std::vector<PatternDefinition *> patternDefinitions;
	std::vector<PatternReference *> patternReferences;
	std::unordered_map<std::string, std::vector<VariableReference *>> variableReferences;
	std::unordered_map<std::string, VariableReference *> variableDefinitions;
	std::vector<CodeLine *> codeLines;
	std::vector<Section *> children;
	std::unordered_map<std::string, Variable *> variables;
	// Monomorphization: each argument type combination gets its own instantiation
	std::map<std::vector<DataType>, Instantiation> instantiations;
	// the start and end index of this section in compiled lines.
	int startLineIndex, endLineIndex;
	// count of unresolved pattern references + unresolved child sections
	int unresolvedCount = 0;
	// whether all pattern definitions in this section are resolved
	bool patternDefinitionsResolved = false;
	// Count of body references containing each VariableLike text.
	// Shared across all definitions in this section since they share the same body.
	// When a count reaches 0, that VL element can be classified as text (Other)
	// without waiting for all body references to resolve.
	std::unordered_map<std::string, int> variableLikeCounts;
	// whether this is a macro (inlined at call site instead of function call)
	bool isMacro = false;
	// recursion guard for type inference of effects/macros
	bool inferring = false;
	// whether this sections patterns can be called from other files
	bool isLocal = false;
	// list of variable names declared as global in this function (from globals: section)
	std::vector<std::string> globalVariables;
	// precedence declarations: patterns that this definition evaluates before/after
	std::vector<std::string> beforePatterns, afterPatterns;
	// Control flow blocks for this section body (set by intrinsics like loop_while, if, etc.)
	// exitBlock: where code continues after this section (always set for control flow)
	// branchBackBlock: if set, branch here at end of body (for loops); null for if/switch
	llvm::BasicBlock *exitBlock{};
	llvm::BasicBlock *branchBackBlock{};
	void collectPatternReferencesAndSections(
		std::list<PatternReference *> &bodyReferences, std::list<PatternReference *> &globalReferences,
		std::list<Section *> &sections, bool insideDefinition = false
	);
	virtual bool processLine(ParseContext &context, CodeLine *line);
	virtual Section *createSection(ParseContext &context, CodeLine *line);
	Function *detectPatterns(ParseContext &context, Range range, SectionType patternType);
	Function *detectPatternsRecursively(ParseContext &context, Range range, StringHierarchy *node, SectionType patternType);
	void addVariableReference(ParseContext &context, VariableReference *reference);
	void searchParentPatterns(ParseContext &context, VariableReference *reference);
	void addPatternReference(PatternReference *reference);
	void incrementUnresolved();
	void decrementUnresolved();

	// Check if this section is a descendant of (nested inside) another section
	bool isDescendantOf(Section *ancestor);

	// Find a Variable by name in this section or parent scopes
	Variable *findVariable(const std::string &name);

	// The line that opens this section (e.g. "loop 10 times:")
	CodeLine *openingLine{};

	virtual std::string toString() const { return openingLine ? std::string(openingLine->patternText) : "main"; }
};
