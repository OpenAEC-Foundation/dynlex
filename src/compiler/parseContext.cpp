#include "parseContext.h"
#include "matchProgress.h"
#include <iostream>

static bool tryParseIntrinsicTypeAlias(Function *intrinsicExpr, DataType &outType) {
	if (!intrinsicExpr || intrinsicExpr->intrinsicName != "type" || intrinsicExpr->arguments.size() < 2)
		return false;

	Function *kindExpr = intrinsicExpr->arguments[1];
	auto *kindStr = std::get_if<std::string>(&kindExpr->literalValue);
	if (!kindStr)
		return false;

	DataType aliasType;
	if (*kindStr == "int") {
		aliasType = {DataType::Kind::Int, 4};
	} else if (*kindStr == "float") {
		aliasType = {DataType::Kind::Float, 8};
	} else if (*kindStr == "bool") {
		aliasType = {DataType::Kind::Bool};
	} else if (*kindStr == "void") {
		aliasType = {DataType::Kind::Void};
	} else if (*kindStr == "string") {
		aliasType = {DataType::Kind::Int, 1};
		aliasType.pointerDepth = 1;
	} else {
		return false;
	}

	if (intrinsicExpr->arguments.size() >= 3) {
		Function *bitsExpr = intrinsicExpr->arguments[2];
		auto *bits = std::get_if<double>(&bitsExpr->literalValue);
		if (!bits)
			return false;
		aliasType.numericSize = (int)*bits / 8;
	}

	outType = aliasType;
	return true;
}

void ParseContext::printDiagnostics() {
	for (Diagnostic d : diagnostics) {
		std::cerr << d.toString() << "\n";
	}
}

PatternMatch *ParseContext::match(PatternReference *reference) {
	MatchProgress progress = MatchProgress(this, reference);
	std::vector<MatchProgress> queue = {progress};
	while (queue.size()) {
		MatchProgress &currentProgress = queue.back();
		std::vector<MatchProgress> nextSteps = currentProgress.step();
		if (currentProgress.isComplete()) {
			return new PatternMatch(currentProgress.match);
		}
		queue.pop_back();
		queue.insert(queue.end(), nextSteps.begin(), nextSteps.end());
	}
	return nullptr;
}

void ParseContext::processEncounteredIntrinsic(Function *intrinsicExpr) {
	if (!intrinsicExpr)
		return;

	CodeLine *line = intrinsicExpr->range.line;
	if (!line || !line->section)
		return;

	Section *replacementSection = line->section;
	if (replacementSection->type != SectionType::Replacement)
		return;
	if (replacementSection->codeLines.size() != 1 || replacementSection->codeLines.front() != line)
		return;
	if (intrinsicExpr->range.start() != 0 || intrinsicExpr->range.end() != (int)line->patternText.size())
		return;

	Section *macroSection = replacementSection->parent;
	if (!macroSection || !macroSection->isMacro || macroSection->type != SectionType::Function ||
		macroSection->patternDefinitions.empty())
		return;

	DataType aliasType;
	if (!tryParseIntrinsicTypeAlias(intrinsicExpr, aliasType))
		return;

	if (typeAliasNames.contains(aliasType))
		return;

	std::string aliasName = (std::string)macroSection->patternDefinitions.front()->range.subString;
	if (!aliasName.empty())
		typeAliasNames.emplace(aliasType, std::move(aliasName));
}
