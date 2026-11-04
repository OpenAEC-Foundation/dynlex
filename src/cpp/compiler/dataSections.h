#pragma once
#include "range.h"
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

struct CodeLine;
struct Expression;
struct ParseContext;
struct Section;
struct ClassDefinition;

struct DataBodyField {
	std::string name;
	Range range;
	Expression *constraint{};
};

struct DataSectionDefinition {
	std::string declarationParameter;
	std::vector<std::unique_ptr<DataBodyField>> fields;
	Expression *resultType{};
	Expression *receiverType{};
	Expression *callbackResultType{};
	ClassDefinition *bodyClass{};
	bool hasValue = false;
};

struct DataSectionValues {
	std::map<std::string, Expression *> fields;
	std::shared_ptr<struct ExpressionStorage> storage;
	Expression *record{};
};

std::optional<Section *> createDataSectionMetadata(ParseContext &context, CodeLine *line, Section *definition);
bool prepareDataSectionCalls(ParseContext &context);
bool prepareDeferredSectionBodies(ParseContext &context);
void prepareDataSectionDefinitions(ParseContext &context);
bool dataSectionFieldVisible(const Section *definition, const struct PatternReference &reference);
bool isDataSectionBodyReference(const struct PatternReference &reference);
const struct VariableMatch *dataSectionDeclaredVariable(const struct PatternMatch &match, struct PatternDefinition &definition);
bool dataSectionDiagnostic(ParseContext &context, Range range, std::string message);
