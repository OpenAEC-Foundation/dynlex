#include "parseContext.h"
#include "compileTimeValue.h"
#include "expression.h"
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
#include <iterator>
#include <unordered_set>

namespace {
void deleteExpressionTree(Expression *expression, std::unordered_set<Expression *> &visited) {
	if (!expression || !visited.insert(expression).second)
		return;
	for (Expression *argument : expression->arguments)
		deleteExpressionTree(argument, visited);
	delete expression;
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

static bool tryParseIntrinsicTypeAlias(Expression *intrinsicExpr, DataType &outType, bool emitSPIRV) {
	if (!intrinsicExpr || intrinsicKind(intrinsicExpr->intrinsicName) != IntrinsicKind::Type ||
		intrinsicExpr->arguments.size() < 2)
		return false;

	Expression *kindExpr = intrinsicExpr->arguments[1];
	auto *kindStr = std::get_if<std::string>(&kindExpr->literalValue);
	if (!kindStr)
		return false;

	DataType aliasType;
	if (*kindStr == "int") {
		aliasType = {DataType::Kind::Int, 4};
	} else if (*kindStr == "float") {
		aliasType = defaultFloatType(emitSPIRV);
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
		Expression *bitsExpr = intrinsicExpr->arguments[2];
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

PatternMatch *ParseContext::match(PatternReference *reference, MatchOptions options) {
	std::vector<MatchProgress> queue;
	queue.emplace_back(this, reference, options);
	size_t steps = 0;
	while (queue.size()) {
		if (options.maxSteps > 0 && steps >= options.maxSteps)
			return nullptr;
		steps++;
		MatchProgress &currentProgress = queue.back();
		std::vector<MatchProgress> nextSteps = currentProgress.step();
		if (currentProgress.isComplete()) {
			return new PatternMatch(currentProgress.match);
		}
		queue.pop_back();
		queue.insert(queue.end(), std::make_move_iterator(nextSteps.begin()), std::make_move_iterator(nextSteps.end()));
	}
	return nullptr;
}

void ParseContext::registerShaderUniformName(const std::string &uniformName, CodeLine *line, int column) {
	if (uniformName.empty())
		return;
	if (line && line->mergedLineIndex >= 0 && column >= 0) {
		ShaderUniformSourceOrder incomingOrder{line->mergedLineIndex, column};
		auto it = shaderUniformSourceOrder.find(uniformName);
		if (it == shaderUniformSourceOrder.end() || std::tie(incomingOrder.mergedLineIndex, incomingOrder.column) <
														std::tie(it->second.mergedLineIndex, it->second.column))
			shaderUniformSourceOrder[uniformName] = incomingOrder;
		return;
	}

	if (std::find(shaderUniformNames.begin(), shaderUniformNames.end(), uniformName) == shaderUniformNames.end())
		shaderUniformNames.push_back(uniformName);
}

void ParseContext::processEncounteredIntrinsic(Expression *intrinsicExpr) {
	if (!intrinsicExpr)
		return;

	if (intrinsicKind(intrinsicExpr->intrinsicName) == IntrinsicKind::ShaderUniform && intrinsicExpr->arguments.size() > 1) {
		if (auto *uniformName = std::get_if<std::string>(&intrinsicExpr->arguments[1]->literalValue))
			registerShaderUniformName(*uniformName, intrinsicExpr->range.line, intrinsicExpr->range.start());
	}

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
	if (!tryParseIntrinsicTypeAlias(intrinsicExpr, aliasType, options.emitSPIRV))
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

namespace {
Expression *cloneMacroExpansionExpressionImpl(ParseContext &context, Expression *expression, bool preserveInferenceMetadata) {
	if (!expression)
		return nullptr;
	Expression *clone = new Expression();
	clone->kind = expression->kind;
	clone->range = expression->range;
	clone->literalValue = expression->literalValue;
	clone->variable = expression->variable;
	clone->patternMatch = expression->patternMatch;
	clone->patternReference = expression->patternReference;
	clone->intrinsicName = expression->intrinsicName;
	clone->inferredMacroExpansion = nullptr;
	clone->isSubMatch = expression->isSubMatch;
	clone->isExplicitGroup = expression->isExplicitGroup;
	clone->groupingArgumentIndices = expression->groupingArgumentIndices;
	clone->groupingArgumentHasAdjacentSiblingSlot = expression->groupingArgumentHasAdjacentSiblingSlot;
	clone->groupingStartsWithArgument = expression->groupingStartsWithArgument;
	clone->groupingEndsWithArgument = expression->groupingEndsWithArgument;
	clone->groupingPrecedence = expression->groupingPrecedence;
	clone->type = preserveInferenceMetadata ? expression->type : DataType{};
	clone->selectedPatternDefinition = preserveInferenceMetadata ? expression->selectedPatternDefinition : nullptr;
	if (preserveInferenceMetadata) {
		CompileTimeValue compileTimeValue =
			getExpressionCompileTimeValue(context, expression, context.currentCodegenInstantiation);
		if (!isCompileTimeKnown(compileTimeValue))
			compileTimeValue = getExpressionCompileTimeValue(context, expression);
		setExpressionCompileTimeValue(context, clone, compileTimeValue);
	}
	clone->arguments.reserve(expression->arguments.size());
	for (Expression *argument : expression->arguments)
		clone->arguments.push_back(cloneMacroExpansionExpressionImpl(context, argument, preserveInferenceMetadata));
	return clone;
}
} // namespace

Expression *ParseContext::cloneMacroExpansionExpression(Expression *expression, bool ownRoot, bool preserveInferenceMetadata) {
	Expression *clone = cloneMacroExpansionExpressionImpl(*this, expression, preserveInferenceMetadata);
	if (clone && ownRoot)
		ownedMacroExpansionRoots.push_back(clone);
	return clone;
}

ParseContext::~ParseContext() {
	std::unordered_set<Expression *> visitedFunctions;
	for (auto &line : ownedCodeLines) {
		if (line && line->expression)
			deleteExpressionTree(line->expression, visitedFunctions);
	}
	for (Expression *expression : ownedMacroExpansionRoots)
		deleteExpressionTree(expression, visitedFunctions);
	for (Expression *expression : ownedCapturedBindingRoots)
		deleteExpressionTree(expression, visitedFunctions);
	for (Expression *expression : ownedCodegenLiteralRoots)
		deleteExpressionTree(expression, visitedFunctions);

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
			if (reference->expression)
				deleteExpressionTree(reference->expression, visitedFunctions);
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
					if (field.declaredType.typeExpression)
						deleteExpressionTree(field.declaredType.typeExpression, visitedFunctions);
					field.declaredType.typeExpression = nullptr;
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
