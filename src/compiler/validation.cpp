#include "bindingResolution.h"
#include "compiler.h"
#include "function.h"
#include "intrinsicInfo.h"
#include <algorithm>

using Bindings = BindingMap;

static bool isInternalSection(Section *section) {
	if (!section)
		return false;
	for (CodeLine *line : section->codeLines) {
		if (line && line->sourceFile && !line->sourceFile->uri.empty())
			return isInternalSourcePath(line->sourceFile->uri);
	}
	return section->openingLine && section->openingLine->sourceFile &&
		   isInternalSourcePath(section->openingLine->sourceFile->uri);
}

static Function *resolveVar(Function *expr, const Bindings &bindings) { return resolveVariableBindingChain(expr, bindings); }

static Bindings buildBindings(Function *expr) {
	Bindings result;
	if (!expr || expr->kind != Function::Kind::PatternCall || !expr->patternMatch || !expr->patternMatch->matchedEndNode)
		return result;
	if (expr->patternMatch->matchedEndNode->matchingDefinitions.empty())
		return result;
	PatternDefinition *def = expr->patternMatch->matchedEndNode->matchingDefinitions[0];
	collectPatternCallBindings(expr, def, result);
	return result;
}

struct VarUsage {
	bool writes = false, reads = false;
};

// Determine whether an function writes to and/or reads from a named variable,
// resolving through macro bindings to reach the underlying intrinsics.
static VarUsage analyzeVariableUsage(
	Function *expr, const std::string &varName, const Bindings &bindings, std::unordered_set<Function *> &visited
) {
	VarUsage usage;
	if (!expr || !visited.insert(expr).second)
		return usage;

	switch (expr->kind) {
	case Function::Kind::IntrinsicCall:
		if (intrinsicKind(expr->intrinsicName) == IntrinsicKind::Store) {
			Function *dest = resolveVar(expr->arguments[1], bindings);
			if (dest && dest->kind == Function::Kind::Variable && dest->variable && dest->variable->name == varName)
				usage.writes = true;
			usage.reads |= analyzeVariableUsage(expr->arguments[2], varName, bindings, visited).reads;
			return usage;
		}
		for (size_t i = 1; i < expr->arguments.size(); i++)
			usage.reads |= analyzeVariableUsage(expr->arguments[i], varName, bindings, visited).reads;
		return usage;

	case Function::Kind::PatternCall: {
		PatternDefinition *def = nullptr;
		if (expr->patternMatch && expr->patternMatch->matchedEndNode &&
			!expr->patternMatch->matchedEndNode->matchingDefinitions.empty())
			def = expr->patternMatch->matchedEndNode->matchingDefinitions[0];
		if (def && def->section && def->section->isMacro) {
			Bindings merged = bindings;
			for (auto &[key, val] : buildBindings(expr))
				merged[key] = resolveVar(val, bindings);
			for (Section *child : def->section->children)
				for (CodeLine *line : child->codeLines)
					if (line->function) {
						VarUsage body = analyzeVariableUsage(line->function, varName, merged, visited);
						usage.writes |= body.writes;
						usage.reads |= body.reads;
					}
			return usage;
		}
		for (Function *arg : expr->arguments)
			usage.reads |= analyzeVariableUsage(arg, varName, bindings, visited).reads;
		return usage;
	}

	case Function::Kind::Variable:
		if (expr->variable) {
			Function *resolved = resolveVar(expr, bindings);
			if (resolved != expr)
				return analyzeVariableUsage(resolved, varName, bindings, visited);
			if (expr->variable->name == varName)
				usage.reads = true;
		}
		return usage;

	default:
		for (Function *arg : expr->arguments) {
			VarUsage a = analyzeVariableUsage(arg, varName, bindings, visited);
			usage.writes |= a.writes;
			usage.reads |= a.reads;
		}
		return usage;
	}
}

static void collectVariableReferences(Section *section, const std::string &name, std::vector<VariableReference *> &refs) {
	auto it = section->variableReferences.find(name);
	if (it != section->variableReferences.end())
		for (VariableReference *ref : it->second)
			refs.push_back(ref);
	for (Section *child : section->children)
		collectVariableReferences(child, name, refs);
}

static void validateSection(ParseContext &context, Section *section) {
	if (isInternalSection(section))
		return;

	for (auto &[name, defRef] : section->variableDefinitions) {
		bool isPatternArg = false;
		if (!section->isMacro)
			for (PatternDefinition *def : section->patternDefinitions)
				forEachLeafElement(def->patternElements, [&](PatternElement &el) {
					if (el.type == PatternElement::Type::Variable && el.text == name)
						isPatternArg = true;
				});

		// Collect references: children only for pattern args (body), whole section for locals
		std::vector<VariableReference *> refs;
		if (isPatternArg) {
			for (Section *child : section->children)
				collectVariableReferences(child, name, refs);
		} else {
			collectVariableReferences(section, name, refs);
		}

		// Find the reference to warn about
		VariableReference *warnRef = nullptr;
		if (isPatternArg) {
			if (refs.empty())
				continue;
			warnRef = *std::min_element(refs.begin(), refs.end(), [](auto *a, auto *b) {
				return a->range.line->mergedLineIndex < b->range.line->mergedLineIndex;
			});
		} else {
			int defLine = defRef->range.line->mergedLineIndex;
			for (VariableReference *ref : refs)
				if (ref != defRef && ref->range.line->mergedLineIndex == defLine) {
					warnRef = ref;
					break;
				}
		}
		if (!warnRef || !warnRef->range.line->function)
			continue;

		std::unordered_set<Function *> visited;
		VarUsage usage = analyzeVariableUsage(warnRef->range.line->function, name, Bindings{}, visited);
		bool problem = isPatternArg ? (usage.writes && !usage.reads) : usage.reads;
		if (!problem)
			continue;

		Diagnostic diag(
			Diagnostic::Level::Warning,
			isPatternArg ? "Pattern argument '" + name + "' is assigned before being read"
						 : "Variable '" + name + "' may be read before being assigned",
			warnRef->range
		);
		diag.relatedInfo.push_back({isPatternArg ? "Defined as argument here" : "Assigned here", defRef->range});
		context.diagnostics.push_back(std::move(diag));
	}

	for (Section *child : section->children)
		validateSection(context, child);
}

bool validate(ParseContext &context) {
	validateSection(context, context.mainSection);
	return true;
}
