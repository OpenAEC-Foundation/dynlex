#include "bindingResolution.h"
#include "compiler.h"
#include "expression.h"
#include "intrinsicInfo.h"
#include "parseContext.h"
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

using Bindings = BindingMap;

static bool isInternalSection(Section *section) {
	for (CodeLine *line : section->codeLines) {
		if (line && line->sourceFile && !line->sourceFile->uri.empty())
			return line->sourceFile->isInternal;
	}
	return section->openingLine && section->openingLine->sourceFile && section->openingLine->sourceFile->isInternal;
}

static Expression *resolveVar(Expression *expr, const Bindings &bindings) {
	return resolveVariableBindingChain(expr, bindings);
}

static Bindings buildBindings(Expression *expr) {
	Bindings result;
	PatternDefinition *def = expr->selectedPatternDefinition;
	requireCompilerInvariant(def != nullptr, "validation requires a finalized pattern overload");
	collectPatternCallBindings(expr, def, result);
	return result;
}

struct VarUsage {
	bool writes = false, reads = false;
};

// Determine whether an expression writes to and/or reads from a named variable,
// resolving through flex bindings to reach the underlying intrinsics.
static VarUsage analyzeVariableUsage(
	Expression *expr, const std::string &varName, const Bindings &bindings, std::unordered_set<Expression *> &visited
) {
	VarUsage usage;
	if (!expr || !visited.insert(expr).second)
		return usage;

	switch (expr->kind) {
	case Expression::Kind::IntrinsicCall:
		if (intrinsicKind(expr->intrinsicName) == IntrinsicKind::Store) {
			Expression *dest = resolveVar(expr->arguments[1], bindings);
			if (dest && dest->kind == Expression::Kind::Variable && dest->variable && dest->variable->name == varName)
				usage.writes = true;
			usage.reads |= analyzeVariableUsage(expr->arguments[2], varName, bindings, visited).reads;
			return usage;
		}
		for (size_t i = 1; i < expr->arguments.size(); i++)
			usage.reads |= analyzeVariableUsage(expr->arguments[i], varName, bindings, visited).reads;
		return usage;

	case Expression::Kind::PatternCall: {
		PatternDefinition *def = expr->selectedPatternDefinition;
		if (def && def->section && def->section->isFlex) {
			Bindings merged = bindings;
			for (auto &[key, val] : buildBindings(expr))
				merged[key] = resolveVar(val, bindings);
			def->section->forEachDefinitionBodySection([&](Section *bodySection) {
				for (CodeLine *line : bodySection->codeLines) {
					if (!line->expression)
						continue;
					VarUsage body = analyzeVariableUsage(line->expression, varName, merged, visited);
					usage.writes |= body.writes;
					usage.reads |= body.reads;
				}
				return true;
			});
			return usage;
		}
		for (Expression *arg : expr->arguments)
			usage.reads |= analyzeVariableUsage(arg, varName, bindings, visited).reads;
		return usage;
	}

	case Expression::Kind::Variable:
		if (expr->variable) {
			Expression *resolved = resolveVar(expr, bindings);
			if (resolved != expr)
				return analyzeVariableUsage(resolved, varName, bindings, visited);
			if (expr->variable->name == varName)
				usage.reads = true;
		}
		return usage;

	default:
		for (Expression *arg : expr->arguments) {
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
		if (!section->isFlex)
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
		if (!warnRef || !warnRef->range.line->expression)
			continue;

		std::unordered_set<Expression *> visited;
		VarUsage usage = analyzeVariableUsage(warnRef->range.line->expression, name, Bindings{}, visited);
		bool problem = isPatternArg ? (usage.writes && !usage.reads) : usage.reads;
		if (!problem)
			continue;

		Diagnostic diag(
			context, Diagnostic::Level::Warning, "variable usage warning",
			isPatternArg ? "pattern argument assigned before read" : "variable read before assigned", warnRef->range, "name",
			name
		);
		diag.relatedInfo.push_back(configuredRelatedInfo(
			context, warnRef->range, "variable usage warning",
			isPatternArg ? "related argument definition" : "related assignment", defRef->range
		));
		context.diagnostics.push_back(std::move(diag));
	}

	for (Section *child : section->children)
		validateSection(context, child);
}

static bool isWithinSection(const Section *section, const Section *ancestor) {
	for (const Section *current = section; current; current = current->parent)
		if (current == ancestor)
			return true;
	return false;
}

static bool validateFunctionCaptures(ParseContext &context, Section *function) {
	bool deferredCallback = function->receiverParameter != nullptr;
	if (!deferredCallback && (function->type != SectionType::Function || function->isFlex))
		return true;
	const auto inspectBody = [&](auto &&self, Section *body) -> bool {
		for (const auto &[name, references] : body->variableReferences) {
			(void)name;
			for (VariableReference *reference : references) {
				requireCompilerInvariant(reference && reference->range.line, "function body has an invalid variable reference");
				VariableReference *definition = normalizeBindingReference(reference);
				requireCompilerInvariant(definition && definition->range.line, "function body variable has no definition");
				Variable *variable = reference->range.section()->findVariable(reference);
				requireCompilerInvariant(variable, "function body variable is absent from its scope");
				Section *owner = reference->range.section();
				while (owner) {
					auto found = owner->variables.find(variable->name);
					if (found != owner->variables.end() && found->second == variable)
						break;
					owner = owner->parent;
				}
				requireCompilerInvariant(owner, "resolved function variable has no owning scope");
				if (variable->isGlobal || isWithinSection(owner, function))
					continue;
				const char *key = deferredCallback ? "deferred callback captures local" : "function captures local";
				Diagnostic diagnostic(
					context, Diagnostic::Level::Error, key, reference->range, "name", variable->name
				);
				const SyntaxConfig &syntax = syntaxConfigForRange(context, reference->range);
				diagnostic.relatedInfo.push_back(
					{renderConfiguredMessage(syntax, key, "related definition"),
					 definition->range}
				);
				context.addDiagnostic(std::move(diagnostic));
				return false;
			}
		}
		for (Section *child : body->children)
			if (child->runsInEnclosingBody() && !self(self, child))
				return false;
		return true;
	};
	for (Section *child : function->children)
		if (child->runsInEnclosingBody() && !inspectBody(inspectBody, child))
			return false;
	return true;
}

bool validate(ParseContext &context) {
	validateSection(context, context.mainSection);
	const auto validateCallbacks = [&](auto &&self, Section *section) -> bool {
		if (!validateFunctionCaptures(context, section))
			return false;
		for (Section *child : section->children)
			if (!self(self, child))
				return false;
		return true;
	};
	return validateCallbacks(validateCallbacks, context.mainSection);
}
