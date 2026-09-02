#pragma once
#include "addressProvenance.h"
#include "codeLine.h"
#include "compileTimeInfo.h"
#include "explicitParameterIndex.h"
#include "patternDefinition.h"
#include "patternReference.h"
#include "range.h"
#include "sectionType.h"
#include "stringHierarchy.h"
#include "type.h"
#include "variableReference.h"
#include <compare>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace llvm {
class Function;
} // namespace llvm

struct ParseContext;
struct Section;
struct Variable;
struct Expression;
struct PatternDefinition;

struct InstantiatedSectionBody {
	Section *sourceSection{};
	std::vector<Expression *> lineExpressions;
	std::vector<std::shared_ptr<InstantiatedSectionBody>> childBodies;

	Expression *&lineExpression(size_t index);
	InstantiatedSectionBody *bodyForChild(Section *child) const;
	Expression *findCloneOf(const Expression *templateExpression) const;
	std::optional<CompileTimeValue> compileTimeValueForReference(const VariableReference *reference) const;
};

enum class InstantiationPurity {
	Pure,
	Impure,
};

enum class DefinitionReturnContract {
	Inferred,
	Nothing,
	Value,
	ReplacementValue,
};

struct InstantiationKey {
	std::vector<DataType> argumentTypes;
	std::vector<std::pair<std::string, CompileTimeValue>> compileTimeParameters;

	auto operator<=>(const InstantiationKey &) const = default;
};

struct FlexExpansionKey {
	PatternDefinition *definition{};
	size_t pathIndex{};
	std::vector<DataType> argumentTypes;
	std::vector<CompileTimeValue> compileTimeArguments;

	bool operator==(const FlexExpansionKey &) const = default;
};

inline bool parameterRequiresCompileTimeInstantiationValue(
	const std::unordered_set<std::string> &requiredCompileTimeParameters, const std::string &parameterName,
	const DataType &argType
) {
	return argType.isMetaType() || requiredCompileTimeParameters.contains(parameterName);
}

template <typename ReadCompileTimeFn>
inline InstantiationKey buildInstantiationKey(
	const std::unordered_set<std::string> &requiredCompileTimeParameters,
	const std::vector<std::pair<std::string, Expression *>> &paramBindings, const std::vector<DataType> &argTypes,
	ReadCompileTimeFn &&readCompileTime
) {
	InstantiationKey key;
	key.argumentTypes = argTypes;
	size_t bindingCount = std::min(paramBindings.size(), argTypes.size());
	for (size_t i = 0; i < bindingCount; i++) {
		if (!parameterRequiresCompileTimeInstantiationValue(requiredCompileTimeParameters, paramBindings[i].first, argTypes[i]))
			continue;
		CompileTimeValue value = readCompileTime(paramBindings[i].second);
		if (std::holds_alternative<std::monostate>(value) && argTypes[i].kind == DataType::Kind::Type)
			value = TypeReferenceValue::exact(argTypes[i]);
		requireCompilerInvariant(
			!std::holds_alternative<std::monostate>(value), "Compile-time instantiation parameter has no value"
		);
		key.compileTimeParameters.push_back({paramBindings[i].first, std::move(value)});
	}
	return key;
}

// Per-instantiation state for monomorphized functions.
// Each unique combination of argument types produces a separate instantiation.
struct Instantiation {
	DataType returnType{DataType::Kind::Any};
	Range returnTypeOriginRange;
	std::vector<DataType> argumentTypes;
	std::unordered_map<std::string, DataType> parameterTypesByName;
	std::unordered_map<std::string, DataType> parameterOutputTypesByName;
	std::unordered_map<std::string, CompileTimeValue> constantParameterValues;
	std::unordered_set<VariableReference *> writtenGlobalReferences;
	std::unordered_map<VariableReference *, CompileTimeValue> finalGlobalConstantValues;
	VariableAddressProvenance finalGlobalAddressProvenance;
	std::unordered_set<VariableReference *> addressTakenGlobalReferences;
	AddressProvenance externallyEscapedGlobalProvenance;
	AddressProvenance returnAddressProvenance;
	bool hasReturnAddressProvenance = false;
	std::optional<std::string> returnPointerStorageParameterName;
	bool returnPointerStorageAmbiguous = false;
	bool writesThroughUnknownAddress = false;
	bool externallyEscapesUnknownAddress = false;
	std::unordered_set<std::string> requiredCompileTimeParameters;
	InstantiationPurity purity = InstantiationPurity::Pure;
	std::map<std::vector<CompileTimeValue>, CompileTimeEvaluation> pureReturnValuesByArguments;
	std::shared_ptr<InstantiatedSectionBody> body;
	llvm::Function *llvmFunction = nullptr;
	llvm::Function *llvmCallableFunction = nullptr;
	bool fallsThrough = true;
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
	ExplicitParameterIndex explicitParameterIndex;
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
	// whether this is a flex (inlined at call site instead of function call)
	bool isFlex = false;
	DefinitionReturnContract returnContract = DefinitionReturnContract::Inferred;
	// whether this sections patterns can be called from other files
	bool isLocal = false;
	// whether this function must be emitted through a stable callable wrapper
	bool isExposed = false;
	// Conversion definitions execute like functions but are indexed in a
	// separate unary pattern tree so they cannot be called as source patterns.
	bool isConversion = false;
	bool isImplicitConversion = false;
	// list of variable names declared as global in this function (from globals: section)
	std::vector<std::string> globalVariables;
	// precedence declarations: patterns that this definition evaluates before/after
	std::vector<std::string> beforePatterns, afterPatterns;
	void collectPatternReferencesAndSections(
		std::list<PatternReference *> &bodyReferences, std::list<PatternReference *> &globalReferences,
		std::list<Section *> &sections, bool insideDefinition = false
	);
	virtual bool processLine(ParseContext &context, CodeLine *line);
	virtual Section *createSection(ParseContext &context, CodeLine *line);
	virtual bool finalize(ParseContext &context);
	Expression *
	detectPatterns(ParseContext &context, Range range, SectionType patternType, bool registerPatternReferences = true);
	Expression *detectPatternsRecursively(
		ParseContext &context, Range range, StringHierarchy *node, SectionType patternType,
		bool registerPatternReferences = true
	);
	void addVariableReference(ParseContext &context, VariableReference *reference);
	void searchParentPatterns(ParseContext &context, VariableReference *reference);
	void indexExplicitParameters(PatternDefinition &definition);
	bool canPromoteImplicitParameter(const PatternDefinition &definition, const DefinitionPatternElement &element) const;
	std::vector<Range> patternParameterCandidateRanges(const std::string &name) const;
	VariableReference *resolvePatternParameterBinding(ParseContext &context, const std::string &name, const Range &useRange);
	void addPatternReference(PatternReference *reference);
	void incrementUnresolved();
	void decrementUnresolved();

	// Check if this section is a descendant of (nested inside) another section
	bool isDescendantOf(Section *ancestor);

	// Find a Variable by name in this section or parent scopes
	Variable *findVariable(const std::string &name);

	// The line that opens this section (e.g. "loop 10 times:")
	CodeLine *openingLine{};

	static bool isDefinitionBodySectionType(SectionType sectionType) {
		return sectionType == SectionType::Get || sectionType == SectionType::Replacement;
	}

	template <typename Visitor> bool forEachDefinitionBodySection(Visitor &&visitor) {
		if (!patternDefinitions.empty()) {
			for (Section *child : children) {
				if (!child || !isDefinitionBodySectionType(child->type))
					continue;
				if (!visitor(child))
					return false;
			}
			return true;
		}
		return visitor(this);
	}

	template <typename Visitor> bool forEachDefinitionBodySection(Visitor &&visitor) const {
		if (!patternDefinitions.empty()) {
			for (const Section *child : children) {
				if (!child || !isDefinitionBodySectionType(child->type))
					continue;
				if (!visitor(child))
					return false;
			}
			return true;
		}
		return visitor(this);
	}

	virtual std::string toString() const { return openingLine ? std::string(openingLine->patternText) : "main"; }
};

template <typename ReadCompileTimeFn>
inline std::optional<InstantiationKey> findMatchingInstantiationKey(
	Section *section, const std::vector<std::pair<std::string, Expression *>> &paramBindings,
	const std::vector<DataType> &argTypes, ReadCompileTimeFn &&readCompileTime
) {
	if (!section)
		return std::nullopt;
	for (const auto &[candidateKey, instantiation] : section->instantiations) {
		if (candidateKey.argumentTypes != argTypes)
			continue;
		if (buildInstantiationKey(instantiation.requiredCompileTimeParameters, paramBindings, argTypes, readCompileTime) ==
			candidateKey) {
			return candidateKey;
		}
	}
	return std::nullopt;
}
