#pragma once
#include "codeLine.h"
#include "compileTimeInfo.h"
#include "patternDefinition.h"
#include "patternReference.h"
#include "sectionType.h"
#include "stringHierarchy.h"
#include "type.h"
#include "variableReference.h"
#include <compare>
#include <list>
#include <map>
#include <optional>
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
struct Expression;
struct PatternDefinition;

struct InstantiationKey {
	std::vector<DataType> argumentTypes;
	std::vector<std::pair<std::string, CompileTimeValue>> compileTimeParameters;

	auto operator<=>(const InstantiationKey &) const = default;
};

inline bool parameterRequiresCompileTimeInstantiationValue(
	const std::unordered_set<std::string> &requiredCompileTimeParameters, const std::string &parameterName,
	const DataType &argType
) {
	return argType.kind == DataType::Kind::Type || requiredCompileTimeParameters.contains(parameterName);
}

template <typename EvaluateCompileTimeFn>
inline InstantiationKey buildInstantiationKey(
	const std::unordered_set<std::string> &requiredCompileTimeParameters,
	const std::vector<std::pair<std::string, Expression *>> &paramBindings, const std::vector<DataType> &argTypes,
	EvaluateCompileTimeFn &&evaluateCompileTime
) {
	InstantiationKey key;
	key.argumentTypes = argTypes;
	size_t bindingCount = std::min(paramBindings.size(), argTypes.size());
	for (size_t i = 0; i < bindingCount; i++) {
		if (!parameterRequiresCompileTimeInstantiationValue(requiredCompileTimeParameters, paramBindings[i].first, argTypes[i]))
			continue;
		if (argTypes[i].kind == DataType::Kind::Type) {
			key.compileTimeParameters.push_back({paramBindings[i].first, argTypes[i]});
			continue;
		}
		key.compileTimeParameters.push_back({paramBindings[i].first, evaluateCompileTime(paramBindings[i].second)});
	}
	return key;
}

// Per-instantiation state for monomorphized functions.
// Each unique combination of argument types produces a separate instantiation.
struct Instantiation {
	struct IfChainSelection {
		bool known = false;
		CodeLine *selectedBranchLine = nullptr;
	};

	DataType returnType{DataType::Kind::Any};
	std::vector<DataType> argumentTypes;
	std::unordered_map<std::string, CompileTimeValue> constantParameterValues;
	std::unordered_map<VariableReference *, CompileTimeValue> constantValuesByReference;
	std::unordered_map<Expression *, CompileTimeValue> constantValuesByExpression;
	std::unordered_set<VariableReference *> writtenGlobalReferences;
	std::unordered_map<VariableReference *, CompileTimeValue> finalGlobalConstantValues;
	std::unordered_map<Expression *, PatternDefinition *> selectedOverloadsByCall;
	std::unordered_map<CodeLine *, IfChainSelection> ifChainSelections;
	std::unordered_set<std::string> requiredCompileTimeParameters;
	llvm::Function *llvmFunction = nullptr;
	llvm::Function *llvmCallableFunction = nullptr;
	bool inferring = false;
	bool needsReinfer = false;
	bool valid = true;
};

struct Section {
	inline Section(SectionType type, Section *parent = {}) : type(type), parent(parent) {
		if (parent) {
			parent->children.push_back(this);
		}
	}
	virtual ~Section() = default;
	SectionType type;
	Section *parent{};
	std::vector<PatternDefinition *> patternDefinitions;
	std::vector<PatternReference *> patternReferences;
	std::unordered_map<std::string, std::vector<VariableReference *>> variableReferences;
	std::unordered_map<std::string, VariableReference *> variableDefinitions;
	std::vector<CodeLine *> codeLines;
	std::vector<Section *> children;
	std::unordered_map<std::string, Variable *> variables;
	// Monomorphization: each unique combination of runtime argument types and
	// compile-time parameter values gets its own instantiation.
	std::map<InstantiationKey, Instantiation> instantiations;
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
	// whether this function must be emitted through a stable callable wrapper
	bool isExposed = false;
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
	virtual bool finalize(ParseContext &context);
	Expression *detectPatterns(ParseContext &context, Range range, SectionType patternType);
	Expression *detectPatternsRecursively(ParseContext &context, Range range, StringHierarchy *node, SectionType patternType);
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

template <typename EvaluateCompileTimeFn>
inline std::optional<InstantiationKey> findMatchingInstantiationKey(
	Section *section, const std::vector<std::pair<std::string, Expression *>> &paramBindings,
	const std::vector<DataType> &argTypes, EvaluateCompileTimeFn &&evaluateCompileTime
) {
	if (!section)
		return std::nullopt;
	for (const auto &[candidateKey, instantiation] : section->instantiations) {
		if (candidateKey.argumentTypes != argTypes)
			continue;
		if (buildInstantiationKey(instantiation.requiredCompileTimeParameters, paramBindings, argTypes, evaluateCompileTime) ==
			candidateKey) {
			return candidateKey;
		}
	}
	return std::nullopt;
}
