#pragma once

#include "operand_reordering.inl"

static bool
inferSection(Section *section, InferenceContext &context, const std::unordered_map<std::string, Function *> &bindings) {
	// The first instantiation determines operand ordering; subsequent ones reuse it.
	// size() > 1 because the current instantiation is already inserted before inferSection is called.
	bool alreadyOrdered = section->instantiations.size() > 1;
	if (context.trial && context.trialJournal)
		context.trialJournal->recordTouchedSection(section);
	resetSectionFunctionTypes(section);
	resetSectionLocalVariableTypes(section);
	for (const auto &[name, boundExpr] : bindings) {
		Variable *boundVar = section->findVariable(name);
		if (!boundVar)
			continue;
		DataType boundType = resolveTypeThroughBindings(boundExpr, bindings);
		if (!boundType.isDeduced())
			continue;
		if (context.trial && context.trialJournal)
			context.trialJournal->recordVariableWrite(boundVar);
		commitVariableTypeFromValue(boundVar, boundExpr, boundType);
	}

	for (CodeLine *line : section->codeLines) {
		if (line->function) {
			if (!inferFunction(line->function, context, alreadyOrdered, bindings)) {
				context.typesValid = false;
				return false;
			}
		}
		if (line->sectionOpening && !dynamic_cast<DefinitionSection *>(line->sectionOpening)) {
			if (!inferSection(line->sectionOpening, context, bindings)) {
				context.typesValid = false;
				return false;
			}
		}
	}
	context.typesValid = true;
	return true;
}

bool inferTypes(ParseContext &parseContext) {
	ActiveTypeResolutionParseContextGuard typeResolutionGuard(parseContext);
	InferenceContext context(parseContext);
	if (!inferSection(parseContext.mainSection, context))
		return false;

	// Validate variables — all must have deduced types
	// Skip non-macro function body sections: their variables only get types during monomorphization
	bool valid = true;
	std::function<void(Section *)> validateVariables = [&](Section *section) {
		if (section->parent && !section->parent->isMacro && !section->parent->patternDefinitions.empty())
			return;
		for (auto &[name, var] : section->variables) {
			if (!var->type.isDeduced()) {
				parseContext.diagnostics.push_back(Diagnostic(
					Diagnostic::Level::Error, "Variable '" + name + "' has no type (never assigned a value)",
					var->definition->range
				));
				valid = false;
			}
		}
		for (Section *child : section->children)
			validateVariables(child);
	};
	validateVariables(parseContext.mainSection);

	// Validate non-macro function functions have deduced return types
	std::function<void(Section *)> validateReturnTypes = [&](Section *section) {
		if (section->type == SectionType::Function && !section->isMacro && !section->patternDefinitions.empty()) {
			for (auto &[argTypes, inst] : section->instantiations) {
				(void)argTypes;
				if (!inst.valid)
					continue;
				if (!inst.returnType.isDeduced()) {
					parseContext.diagnostics.push_back(Diagnostic(
						Diagnostic::Level::Error,
						"Function '" + (std::string)section->patternDefinitions.front()->range.subString +
							"' has no deduced return type",
						section->patternDefinitions.front()->range
					));
					valid = false;
					break; // one error per section is enough
				}
			}
		}
		for (Section *child : section->children)
			validateReturnTypes(child);
	};
	validateReturnTypes(parseContext.mainSection);

	return valid;
}

bool ensureSectionInstantiationInferred(
	ParseContext &parseContext, Section *section, const std::unordered_map<std::string, Function *> &callBindings,
	const std::vector<DataType> &argTypes, const Instantiation *callerInstantiation
) {
	ActiveTypeResolutionParseContextGuard typeResolutionGuard(parseContext);
	if (!section)
		return false;

	Instantiation &inst = section->instantiations[argTypes];
	for (const auto &[name, argExpr] : callBindings) {
		CompileTimeValue value = evaluateCompileTimeValue(argExpr, parseContext, {}, callerInstantiation);
		if (isCompileTimeKnown(value))
			inst.constantParameterValues[name] = value;
		else
			inst.constantParameterValues.erase(name);
	}
	if (inst.returnType.isDeduced())
		return inst.valid;
	if (inst.inferring)
		return inst.returnType.isDeduced() && inst.valid;

	InferenceContext context(parseContext);
	inst.inferring = true;
	Instantiation *savedInst = context.currentInstantiation;
	context.currentInstantiation = &inst;
	bool inferenceSucceeded = inferSection(section, context, callBindings);
	context.currentInstantiation = savedInst;
	inst.inferring = false;
	inst.valid = inferenceSucceeded;
	if (!inst.valid || !context.typesValid)
		return false;

	if (inst.returnType.kind == DataType::Kind::Any)
		inst.returnType = {DataType::Kind::Void};

	return inst.returnType.isDeduced();
}
