#include "scopeRulesSection.h"
#include "definitionSection.h"
#include "parseContext.h"
#include "syntaxConfig.h"
#include <utility>
#include <vector>

namespace {

std::string_view scopePhrase(const SyntaxConfig &syntax, ScopeRulesSection::Scope scope) {
	switch (scope) {
	case ScopeRulesSection::Scope::File:
		return syntax.fileScopeName;
	case ScopeRulesSection::Scope::Class:
		return syntax.classScopeName;
	case ScopeRulesSection::Scope::Function:
		return syntax.functionScopeName;
	}
	crashCompilerBug("unknown scope rule scope");
}

bool scopeAllowsOwner(const ParseContext &context, ScopeRulesSection::Scope scope, const Section *owner) {
	switch (scope) {
	case ScopeRulesSection::Scope::File:
		return owner == context.mainSection;
	case ScopeRulesSection::Scope::Class:
		return owner->type == SectionType::Class;
	case ScopeRulesSection::Scope::Function:
		return dynamic_cast<const DefinitionSection *>(owner) && owner->type != SectionType::Class;
	}
	crashCompilerBug("unknown scope rule scope");
}

std::string_view misplacedMessageKey(ScopeRulesSection::Scope scope) {
	switch (scope) {
	case ScopeRulesSection::Scope::File:
		return "file scope rules misplaced";
	case ScopeRulesSection::Scope::Class:
		return "class scope rules misplaced";
	case ScopeRulesSection::Scope::Function:
		return "function scope rules misplaced";
	}
	crashCompilerBug("unknown scope rule scope");
}

LocalDefinitionKind localDefinitionKind(const Section *definition) {
	if (definition->type == SectionType::Class)
		return LocalDefinitionKind::Class;
	if (definition->type == SectionType::Section)
		return LocalDefinitionKind::Section;
	return definition->isConversion ? LocalDefinitionKind::Conversion : LocalDefinitionKind::Function;
}

const ScopeRuleSource *
findLocalityRule(const ParseContext &context, const CodeLine *line, const Section *owner, LocalDefinitionKind kind) {
	const size_t index = static_cast<size_t>(kind);
	for (const Section *scope = owner; scope && scope != context.mainSection; scope = scope->parent) {
		if (const std::optional<ScopeRuleSource> &rule = scope->scopeRules.local[index])
			return &*rule;
	}
	auto fileRules = context.fileScopeRules.find(line->sourceFile);
	if (fileRules == context.fileScopeRules.end() || !fileRules->second.local[index])
		return nullptr;
	return &*fileRules->second.local[index];
}

} // namespace

std::optional<Section *> createScopeRulesSection(ParseContext &context, CodeLine *line, Section *owner) {
	const SyntaxConfig &syntax = syntaxConfigForSourceFile(context, line->sourceFile);
	std::string_view text = line->patternText;
	for (ScopeRulesSection::Scope scope :
		 {ScopeRulesSection::Scope::File, ScopeRulesSection::Scope::Class, ScopeRulesSection::Scope::Function}) {
		std::string_view phrase = scopePhrase(syntax, scope);
		bool absolute = text == phrase;
		bool byDefault = !absolute && text.size() == phrase.size() + syntax.defaultScopeMarker.size() &&
						 text.starts_with(phrase) && text.ends_with(syntax.defaultScopeMarker);
		if (!absolute && !byDefault)
			continue;
		Range headerRange(line, text);
		if (!scopeAllowsOwner(context, scope, owner)) {
			context.addDiagnostic(
				Diagnostic(context, Diagnostic::Level::Error, misplacedMessageKey(scope), headerRange, "scope", phrase)
			);
			return nullptr;
		}
		for (CodeLine *previous : owner->codeLines) {
			if (previous == line)
				break;
			if (previous->sourceFile != line->sourceFile || previous->patternText.empty())
				continue;
			if (previous->sectionOpening && previous->sectionOpening->type == SectionType::ScopeRules)
				continue;
			if (scope == ScopeRulesSection::Scope::File &&
				extractDirectiveArgument(previous->patternText, syntax.importKeyword))
				continue;
			Diagnostic diagnostic(context, Diagnostic::Level::Error, "scope rules not first", headerRange);
			diagnostic.relatedInfo.push_back(
				{renderConfiguredMessage(syntax, "scope rules not first", "related line"),
				 Range(previous, previous->patternText)}
			);
			context.addDiagnostic(std::move(diagnostic));
			return nullptr;
		}
		ScopeRules &rules =
			scope == ScopeRulesSection::Scope::File ? context.fileScopeRules[line->sourceFile] : owner->scopeRules;
		bool &hasBlock = absolute ? rules.hasAbsoluteBlock : rules.hasDefaultBlock;
		if (hasBlock) {
			context.addDiagnostic(Diagnostic(context, Diagnostic::Level::Error, "scope rules block repeated", headerRange));
			return nullptr;
		}
		hasBlock = true;
		return new ScopeRulesSection(owner, rules, scope, absolute);
	}
	return std::nullopt;
}

bool ScopeRulesSection::processLine(ParseContext &context, CodeLine *line) {
	const SyntaxConfig &syntax = syntaxConfigForSourceFile(context, line->sourceFile);
	std::string_view text = line->patternText;
	std::vector<LocalDefinitionKind> kinds;
	if (text == syntax.everythingLocalRule)
		kinds = {
			LocalDefinitionKind::Function, LocalDefinitionKind::Class, LocalDefinitionKind::Section,
			LocalDefinitionKind::Conversion
		};
	else if (text == syntax.functionsLocalRule)
		kinds = {LocalDefinitionKind::Function};
	else if (text == syntax.classesLocalRule)
		kinds = {LocalDefinitionKind::Class};
	else if (text == syntax.sectionsLocalRule)
		kinds = {LocalDefinitionKind::Section};
	else if (text == syntax.conversionsLocalRule)
		kinds = {LocalDefinitionKind::Conversion};

	if (kinds.empty()) {
		// Every other rule is an ordinary pattern, such as `numbers are 64 bit floating-point`.
		line->expression = detectPatterns(context, Range(line, text), SectionType::Function);
		return line->expression != nullptr;
	}

	Range ruleRange(line, text);
	if (scope == Scope::Class) {
		context.addDiagnostic(Diagnostic(context, Diagnostic::Level::Error, "locality rule in class", ruleRange));
		return false;
	}
	for (LocalDefinitionKind kind : kinds) {
		std::optional<ScopeRuleSource> &slot = rules.local[static_cast<size_t>(kind)];
		if (slot) {
			Diagnostic diagnostic(context, Diagnostic::Level::Error, "scope rule repeated", ruleRange, "rule", text);
			diagnostic.relatedInfo.push_back(
				{renderConfiguredMessage(syntax, "scope rule repeated", "related rule"), slot->range}
			);
			context.addDiagnostic(std::move(diagnostic));
			return false;
		}
		slot = ScopeRuleSource{ruleRange, absolute};
	}
	context.addSourceToken(ruleRange, ParseContext::SourceTokenKind::Keyword);
	line->resolved = true;
	return true;
}

Section *ScopeRulesSection::createSection(ParseContext &context, CodeLine *line) {
	context.addDiagnostic(
		Diagnostic(context, Diagnostic::Level::Error, "scope rules cannot contain sections", Range(line, line->patternText))
	);
	return nullptr;
}

bool applyDefinitionLocality(
	ParseContext &context, CodeLine *line, Section *owner, Section *definition, std::optional<Range> localModifier,
	std::optional<Range> publicModifier
) {
	const SyntaxConfig &syntax = syntaxConfigForSourceFile(context, line->sourceFile);
	if (localModifier && publicModifier) {
		context.addDiagnostic(Diagnostic(
			context, Diagnostic::Level::Error, "local and public modifiers", *publicModifier, "local", syntax.localName,
			"public", syntax.publicName
		));
		return false;
	}
	const ScopeRuleSource *rule = findLocalityRule(context, line, owner, localDefinitionKind(definition));
	if (!rule) {
		if (publicModifier) {
			context.addDiagnostic(Diagnostic(
				context, Diagnostic::Level::Error, "public modifier redundant", *publicModifier, "modifier", syntax.publicName
			));
			return false;
		}
		definition->isLocal = localModifier.has_value();
		return true;
	}
	if (localModifier) {
		Diagnostic diagnostic(
			context, Diagnostic::Level::Error, "local modifier redundant", *localModifier, "modifier", syntax.localName
		);
		diagnostic.relatedInfo.push_back(
			{renderConfiguredMessage(syntax, "local modifier redundant", "related rule"), rule->range}
		);
		context.addDiagnostic(std::move(diagnostic));
		return false;
	}
	if (publicModifier && rule->absolute) {
		Diagnostic diagnostic(
			context, Diagnostic::Level::Warning, "public modifier contradicts rule", *publicModifier, "modifier",
			syntax.publicName, "rule", rule->range.subString
		);
		diagnostic.relatedInfo.push_back(
			{renderConfiguredMessage(syntax, "public modifier contradicts rule", "related rule"), rule->range}
		);
		context.addDiagnostic(std::move(diagnostic));
	}
	definition->isLocal = !publicModifier;
	return true;
}
