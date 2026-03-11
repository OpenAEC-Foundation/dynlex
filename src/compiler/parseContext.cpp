#include "parseContext.h"
#include "function.h"
#include "intrinsicInfo.h"
#include "matchProgress.h"
#include "patternDefinition.h"
#include "patternReference.h"
#include "patternTreeNode.h"
#include "section/classSection.h"
#include "section/variable.h"
#include "variableReference.h"
#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/ManagedStatic.h"
#include <iostream>
#include <unordered_set>

namespace {
void deleteFunctionTree(Function *function, std::unordered_set<Function *> &visited) {
	if (!function || !visited.insert(function).second)
		return;
	for (Function *argument : function->arguments)
		deleteFunctionTree(argument, visited);
	delete function;
}

void deletePatternTree(PatternTreeNode *node, std::unordered_set<PatternTreeNode *> &visited) {
	if (!node || !visited.insert(node).second)
		return;
	for (auto &[_, child] : node->literalChildren)
		deletePatternTree(child, visited);
	deletePatternTree(node->argumentChild, visited);
	deletePatternTree(node->wordChild, visited);
	delete node;
}
} // namespace

static bool tryParseIntrinsicTypeAlias(Function *intrinsicExpr, DataType &outType) {
	if (!intrinsicExpr || intrinsicKind(intrinsicExpr->intrinsicName) != IntrinsicKind::Type ||
		intrinsicExpr->arguments.size() < 2)
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
	} else if (*kindStr == "type") {
		aliasType = {DataType::Kind::Type};
	} else {
		return false;
	}

	if (intrinsicExpr->arguments.size() > 2) {
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

VariableReference *ParseContext::createVariableReference(Range range, const std::string &name) {
	ownedVariableReferences.push_back(std::make_unique<VariableReference>(std::move(range), name));
	return ownedVariableReferences.back().get();
}

ParseContext::~ParseContext() {
	std::unordered_set<Function *> visitedFunctions;
	for (auto &line : ownedCodeLines) {
		if (line && line->function)
			deleteFunctionTree(line->function, visitedFunctions);
	}

	std::unordered_set<Section *> visitedSections;
	std::unordered_set<PatternDefinition *> visitedDefinitions;
	std::unordered_set<PatternReference *> visitedReferences;
	std::unordered_set<Variable *> visitedVars;
	std::vector<Section *> sectionStack;
	if (mainSection)
		sectionStack.push_back(mainSection);

	while (!sectionStack.empty()) {
		Section *section = sectionStack.back();
		sectionStack.pop_back();
		if (!section || !visitedSections.insert(section).second)
			continue;
		for (Section *child : section->children)
			sectionStack.push_back(child);

		for (PatternDefinition *definition : section->patternDefinitions) {
			if (definition && visitedDefinitions.insert(definition).second)
				delete definition;
		}
		for (PatternReference *reference : section->patternReferences) {
			if (!reference || !visitedReferences.insert(reference).second)
				continue;
			if (reference->function)
				deleteFunctionTree(reference->function, visitedFunctions);
			delete reference->match;
			reference->match = nullptr;
			delete reference;
		}
		for (auto &[_, variable] : section->variables) {
			if (variable && visitedVars.insert(variable).second)
				delete variable;
		}
		if (section->type == SectionType::Class) {
			auto *classSection = static_cast<ClassSection *>(section);
			if (classSection->classDefinition) {
				for (FieldDefinition &field : classSection->classDefinition->fields) {
					if (field.declaredType.typeFunction)
						deleteFunctionTree(field.declaredType.typeFunction, visitedFunctions);
					field.declaredType.typeFunction = nullptr;
				}
				delete classSection->classDefinition;
				classSection->classDefinition = nullptr;
			}
		}
	}

	for (Section *section : visitedSections)
		delete section;
	mainSection = nullptr;

	if (hasCompleted(CompilationStage::ResolvedPatterns)) {
		std::unordered_set<PatternTreeNode *> visitedPatternNodes;
		for (PatternTreeNode *&tree : patternTrees) {
			deletePatternTree(tree, visitedPatternNodes);
			tree = nullptr;
		}
	}

	delete diBuilder;
	diBuilder = nullptr;
	delete static_cast<llvm::IRBuilder<> *>(llvmBuilder);
	llvmBuilder = nullptr;
	delete llvmModule;
	llvmModule = nullptr;
	delete llvmContext;
	llvmContext = nullptr;
	llvm::llvm_shutdown();
}
