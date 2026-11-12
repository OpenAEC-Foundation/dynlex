#include "validation.h"
#include "bindingResolution.h"
#include "compiler.h"
#include "expression.h"
#include "parseContext.h"
#include <algorithm>
#include <tuple>

static bool isWithinSection(const Section *section, const Section *ancestor) {
	for (const Section *current = section; current; current = current->parent)
		if (current == ancestor)
			return true;
	return false;
}

std::vector<VariableReference *> captureReferences(Section *function, const std::function<void(Section *)> &observe) {
	std::vector<VariableReference *> references;
	const auto collect = [&](auto &&self, Section *body) -> void {
		if (observe)
			observe(body);
		for (const auto &[name, group] : body->variableReferences) {
			(void)name;
			references.insert(references.end(), group.begin(), group.end());
		}
		for (Section *child : body->children)
			if (child->runsInEnclosingBody())
				self(self, child);
	};
	for (Section *child : function->children)
		if (child->runsInEnclosingBody())
			collect(collect, child);
	std::sort(references.begin(), references.end(), [](const auto *left, const auto *right) {
		return std::tuple(left->range.line->mergedLineIndex, left->range.start(), left->range.end(), left->name) <
			   std::tuple(right->range.line->mergedLineIndex, right->range.start(), right->range.end(), right->name);
	});
	return references;
}

static bool validateFunctionCaptures(ParseContext &context, Section *function, const ValidationHooks &hooks) {
	bool deferredCallback = function->receiverParameter != nullptr;
	auto read = [&](Section *section, ValidationRead kind, std::string_view name = {}) {
		if (hooks.read)
			hooks.read(section, kind, name);
	};
	read(function, ValidationRead::Definition);
	read(function, ValidationRead::Children);
	auto references = captureReferences(function, [&](Section *body) {
		read(body, ValidationRead::References);
		read(body, ValidationRead::Children);
	});
	for (VariableReference *reference : references) {
		if (hooks.work)
			hooks.work->referencesVisited++;
		requireCompilerInvariant(reference && reference->range.line, "function body has an invalid variable reference");
		VariableReference *definition = normalizeBindingReference(reference);
		requireCompilerInvariant(definition && definition->range.line, "function body variable has no definition");
		Variable *variable = reference->range.section()->findVariable(reference);
		requireCompilerInvariant(variable, "function body variable is absent from its scope");
		Section *owner = reference->range.section();
		while (owner) {
			read(owner, ValidationRead::Binding, variable->name);
			auto found = owner->variables.find(variable->name);
			if (found != owner->variables.end() && found->second == variable)
				break;
			owner = owner->parent;
		}
		requireCompilerInvariant(owner, "resolved function variable has no owning scope");
		if (variable->isGlobal || isWithinSection(owner, function))
			continue;
		read(function, ValidationRead::DiagnosticOrder);
		const char *key = deferredCallback ? "deferred callback captures local" : "function captures local";
		Diagnostic diagnostic(context, Diagnostic::Level::Error, key, reference->range, "name", variable->name);
		diagnostic.relatedInfo.push_back(
			configuredRelatedInfo(context, reference->range, key, "related definition", definition->range)
		);
		context.addDiagnostic(std::move(diagnostic));
		return false;
	}
	return true;
}

bool validate(ParseContext &context) { return validate(context, {}); }

bool validate(ParseContext &context, const ValidationHooks &hooks) {
	const auto validateCallbacks = [&](auto &&self, Section *section) -> bool {
		if (section->receiverParameter || (section->type == SectionType::Function && !section->isFlex)) {
			auto execute = [&]() {
				return validateFunctionCaptures(context, section, hooks);
			};
			if (!(hooks.unit ? hooks.unit(section, execute) : execute()))
				return false;
		}
		for (Section *child : section->children)
			if (!self(self, child))
				return false;
		return true;
	};
	return validateCallbacks(validateCallbacks, context.mainSection);
}
