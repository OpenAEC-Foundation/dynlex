#include "dataSections.h"
#include "compiler.h"
#include "diagnostic.h"
#include "parseContext.h"
#include "pattern/patternReference.h"
#include "section/classSection.h"
#include "section/definitionSection.h"
#include "section/replacementSection.h"
#include "section/section.h"
#include "section/sectionSection.h"
#include "section/variable.h"
#include "syntaxConfig.h"
#include <algorithm>

bool dataSectionDiagnostic(ParseContext &context, Range range, std::string message) {
	Diagnostic diagnostic;
	diagnostic.range = range;
	diagnostic.message = std::move(message);
	context.addDiagnostic(std::move(diagnostic));
	return false;
}

namespace {

struct MetadataSection : Section {
	MetadataSection(Section *parent) : Section(SectionType::Custom, parent) {}
	bool runsInEnclosingBody() const override { return false; }
};

struct FieldSection : MetadataSection {
	bool isValueExpressionContext() const override { return true; }
	DataBodyField &field;
	FieldSection(Section *parent, DataBodyField &field) : MetadataSection(parent), field(field) {}
	bool processLine(ParseContext &context, CodeLine *line) override {
		if (field.constraint)
			return dataSectionDiagnostic(
				context, Range(line, line->patternText), "A body field requires one constraint expression"
			);
		field.constraint = detectPatterns(context, Range(line, line->patternText), SectionType::Function);
		line->expression = field.constraint;
		return field.constraint != nullptr;
	}
	bool finalize(ParseContext &context) override {
		return field.constraint || dataSectionDiagnostic(context, field.range, "A body field requires a constraint");
	}
};

struct BodySection : MetadataSection {
	DataSectionDefinition &definition;
	BodySection(ParseContext &context, Section *parent, DataSectionDefinition &definition, Range declarationRange)
		: MetadataSection(parent), definition(definition) {
		auto *record = new ClassSection(context.mainSection, declarationRange);
		record->declarationProducer = parent;
		definition.bodyClass = record->classDefinition;
		record->classDefinition->patternNames.push_back("section body");
		record->classDefinition->displayPatternNames.push_back("section body");
	}
	bool processLine(ParseContext &context, CodeLine *line) override {
		return dataSectionDiagnostic(
			context, Range(line, line->patternText), "A body schema requires named fields followed by ':'"
		);
	}
	Section *createSection(ParseContext &context, CodeLine *line) override {
		std::string name(line->patternText);
		if (name.empty() ||
			name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_ ") != std::string::npos) {
			dataSectionDiagnostic(context, Range(line, line->patternText), "Invalid section body field name");
			return nullptr;
		}
		if (std::ranges::any_of(definition.fields, [&](const auto &field) {
			return field->name == name;
		})) {
			dataSectionDiagnostic(context, Range(line, line->patternText), "Duplicate section body field: " + name);
			return nullptr;
		}
		auto field = std::make_unique<DataBodyField>();
		field->name = name;
		field->range = Range(line, line->patternText);
		definition.fields.push_back(std::move(field));
		definition.bodyClass->fields.push_back({name, Range(line, line->patternText), {DataType::Kind::Any}});
		return new FieldSection(this, *definition.fields.back());
	}
};

struct BindingSection : MetadataSection {
	DataSectionDefinition &definition;
	BindingSection(Section *parent, DataSectionDefinition &definition) : MetadataSection(parent), definition(definition) {}
	bool processLine(ParseContext &context, CodeLine *line) override {
		if (!definition.declarationParameter.empty())
			return dataSectionDiagnostic(
				context, Range(line, line->patternText), "A data section can declare one header parameter"
			);
		definition.declarationParameter = std::string(line->patternText);
		line->resolved = true;
		return true;
	}
};

struct ResultTypeSection : MetadataSection {
	bool isValueExpressionContext() const override { return true; }
	Expression *&expression;
	ResultTypeSection(Section *parent, Expression *&expression) : MetadataSection(parent), expression(expression) {}
	bool processLine(ParseContext &context, CodeLine *line) override {
		if (expression)
			return dataSectionDiagnostic(
				context, Range(line, line->patternText), "Section metadata requires one type expression"
			);
		expression = detectPatterns(context, Range(line, line->patternText), SectionType::Function);
		line->expression = expression;
		return expression != nullptr;
	}
};

} // namespace

std::optional<Section *> createDataSectionMetadata(ParseContext &context, CodeLine *line, Section *definition) {
	if (definition->type != SectionType::Section)
		return std::nullopt;
	const auto &syntax = syntaxConfigForSourceFile(context, line->sourceFile);
	const std::string_view name = line->patternText;
	if (name != syntax.dataBodyName && name != syntax.dataValueName && name != syntax.dataDeclaresName &&
		name != syntax.dataResultName && name != syntax.dataReceiverName && name != syntax.dataReturnsName)
		return std::nullopt;
	if (!definition->dataSection)
		definition->dataSection = std::make_shared<DataSectionDefinition>();
	auto &data = *definition->dataSection;
	if (name == syntax.dataBodyName) {
		if (data.bodyClass || data.receiverType) {
			dataSectionDiagnostic(context, Range(line, name), "A section accepts one data body or one deferred body");
			return static_cast<Section *>(nullptr);
		}
		return new BodySection(context, definition, data, Range(line, line->patternText));
	}
	if (name == syntax.dataDeclaresName)
		return new BindingSection(definition, data);
	if (name == syntax.dataResultName)
		return new ResultTypeSection(definition, data.resultType);
	if (name == syntax.dataReturnsName)
		return new ResultTypeSection(definition, data.callbackResultType);
	if (name == syntax.dataReceiverName) {
		if (data.bodyClass) {
			dataSectionDiagnostic(context, Range(line, name), "A section accepts one data body or one deferred body");
			return static_cast<Section *>(nullptr);
		}
		return new ResultTypeSection(definition, data.receiverType);
	}
	if (data.hasValue) {
		dataSectionDiagnostic(context, Range(line, name), "A data section can have only one value block");
		return static_cast<Section *>(nullptr);
	}
	data.hasValue = true;
	definition->isFlex = true;
	auto *result = new ReplacementSection(definition);
	static_cast<DefinitionSection *>(definition)->executionSection = result;
	return result;
}

bool prepareDataSectionCalls(ParseContext &context) {
	for (CodeLine *line : context.codeLines) {
		if (!line->sectionOpening || !line->expression.rootReference())
			continue;
		auto definitions = context.sourceDefinitions(line->expression.rootReference());
		DataSectionDefinition *data = nullptr;
		for (PatternDefinition *definition : definitions) {
			if (definition->section->dataSection) {
				if (data && data != definition->section->dataSection.get())
					return dataSectionDiagnostic(
						context, Range(line, line->patternText), "A data section header must identify one body schema"
					);
				data = definition->section->dataSection.get();
			}
		}
		if (!data)
			continue;
		if (context.demandBodyResolution)
			context.demandBodyResolution(line->section);
		Expression *call = line->expression;
		if (!data->hasValue)
			return dataSectionDiagnostic(context, call->range, "A data section requires a value block");
		if (data->declarationParameter.empty())
			continue;
		PatternDefinition *definition = call->patternMatch->matchingDefinitions.front();
		std::vector<std::pair<std::string, Expression *>> bindings;
		auto paths = matchingPatternPathIndices(call->patternMatch->nodesPassed, definition);
		requireCompilerInvariant(paths.size() == 1, "data binding has multiple signature paths");
		collectPatternCallBindingPairsForPath(call, definition, paths.front(), bindings);
		auto binding = std::ranges::find_if(bindings, [&](const auto &entry) {
			return entry.first == data->declarationParameter;
		});
		if (binding == bindings.end() || binding->second->kind != Expression::Kind::Variable)
			return dataSectionDiagnostic(context, call->range, "A declared section name must be a variable binding");
		Expression *target = binding->second;
		auto *label = new Expression();
		label->kind = Expression::Kind::Literal;
		label->range = target->range;
		label->literalValue = target->variable->name;
		for (Expression *&argument : call->arguments) {
			if (argument == target)
				argument = label;
		}
		call->dataDeclaration = target;
		Variable *variable = target->range.section()->findVariable(target->variable);
		requireCompilerInvariant(variable, "data declaration binding has no variable");
		if (variable->dataInitializer && variable->dataInitializer != call)
			return dataSectionDiagnostic(context, target->range, "Duplicate data declaration: " + variable->name);
		variable->dataInitializer = call;
	}
	return true;
}

void prepareDataSectionDefinitions(ParseContext &context) {
	std::vector<Section *> definitions;
	std::function<void(Section *)> collect = [&](Section *section) {
		if (section->dataSection)
			definitions.push_back(section);
		for (Section *child : section->children)
			collect(child);
	};
	collect(context.mainSection);
	for (Section *definition : definitions) {
		for (const auto &field : definition->dataSection->fields) {
			auto *header = new SectionSection(context.mainSection);
			header->declarationProducer = definition;
			header->dataFieldOwner = definition;
			header->dataField = field.get();
			header->patternDefinitions.push_back(new PatternDefinition(field->range, header));
		}
	}
}

bool dataSectionFieldVisible(const Section *definition, const PatternReference &reference) {
	if (!definition->dataFieldOwner)
		return true;
	Section *scope = reference.matchingScope;
	if (!scope || !scope->openingLine || !scope->openingLine->expression)
		return false;
	Expression *header = scope->openingLine->expression;
	PatternMatch *match = header->patternReference ? header->patternReference->match : header->patternMatch;
	return match && std::ranges::any_of(match->matchingDefinitions, [&](PatternDefinition *candidate) {
		return candidate->section == definition->dataFieldOwner;
	});
}

bool isDataSectionBodyReference(const PatternReference &reference) {
	for (Section *scope = reference.matchingScope; scope && scope->type == SectionType::Custom; scope = scope->parent) {
		Expression *header = scope->openingLine ? scope->openingLine->expression : nullptr;
		PatternMatch *match = header && header->patternReference ? header->patternReference->match : nullptr;
		if (!match)
			continue;
		for (PatternDefinition *definition : match->matchingDefinitions) {
			if (definition->section->dataSection)
				return !definition->section->dataSection->receiverType;
		}
	}
	return false;
}

const VariableMatch *dataSectionDeclaredVariable(const PatternMatch &match, PatternDefinition &definition) {
	if (!definition.section->dataSection || definition.section->dataSection->declarationParameter.empty())
		return nullptr;
	size_t argumentIndex = 0;
	for (PatternTreeNode *node : match.nodesPassed) {
		if (node->type != PatternElement::Type::Variable)
			continue;
		auto occurrences = node->definitionOccurrences.find(&definition);
		if (occurrences != node->definitionOccurrences.end() &&
			std::ranges::any_of(occurrences->second, [&](const auto &occurrence) {
			return matchedPatternParameterElement(
					   &definition, definition.section->dataSection->declarationParameter, occurrence.startPos
				   ) != nullptr;
		})) {
			for (const MatchedArgument &argument : match.orderedArguments) {
				if (argument.argumentIndex == argumentIndex && argument.kind == MatchedArgument::Kind::Variable)
					return &match.discoveredVariables.at(argument.itemIndex);
			}
		}
		argumentIndex++;
	}
	return nullptr;
}
