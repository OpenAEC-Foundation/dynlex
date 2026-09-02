#include "matchProgress.h"
#include "parseContext.h"
#include "patternDefinition.h"
#include "patternReference.h"
#include <algorithm>
#include <iterator>
#include <limits>

namespace {
static void combineMatchStateHash(size_t &seed, size_t value) {
	seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
}

static size_t firstDefinitionStartPos(const PatternTreeNode *node) {
	if (!node)
		return std::numeric_limits<size_t>::max();
	size_t firstStartPos = std::numeric_limits<size_t>::max();
	for (const auto &[ignoredDefinition, occurrences] : node->definitionOccurrences) {
		for (const PatternDefinitionOccurrence &occurrence : occurrences)
			firstStartPos = std::min(firstStartPos, occurrence.startPos);
	}
	return firstStartPos;
}

static std::string consumedSourcePrefix(const PatternReference *reference, size_t elementIndex, size_t charIndex) {
	if (!reference)
		return {};
	const std::vector<PatternElement> &elements = reference->patternElements;
	size_t boundedElementIndex = std::min(elementIndex, elements.size());
	std::string result;
	for (size_t i = 0; i < boundedElementIndex; i++)
		result += elements[i].text;
	if (boundedElementIndex < elements.size() && charIndex > 0) {
		const std::string &currentText = elements[boundedElementIndex].text;
		size_t prefixLength = std::min(charIndex, currentText.size());
		result += currentText.substr(0, prefixLength);
	}
	return result;
}

struct RemainingPatternElement {
	bool exists = false;
	PatternElement::Type type = PatternElement::Type::Count;
	std::string_view text;
};

struct ExplicitVariableCandidate {
	std::string name;
	size_t elementCount{};
	size_t textLength{};
};

static bool
explicitVariableDeclarationPrecedes(const Range &declaration, const PatternReference &reference, size_t sourceElementIndex) {
	requireCompilerInvariant(
		declaration.line && reference.range().line && sourceElementIndex < reference.patternElements.size(),
		"explicit variable visibility check has no source position"
	);
	int useStart = reference.range().start() + static_cast<int>(reference.patternElements[sourceElementIndex].startPos);
	return std::pair(declaration.line->mergedLineIndex, declaration.start()) <
		   std::pair(reference.range().line->mergedLineIndex, useStart);
}

static std::vector<ExplicitVariableCandidate> explicitMultiWordVariableCandidates(const MatchProgress &progress) {
	std::vector<ExplicitVariableCandidate> result;
	if (!progress.patternReference || progress.sourceCharIndex != 0 || !progress.patternReference->range().line)
		return result;
	const std::vector<PatternElement> &sourceElements = progress.patternReference->patternElements;
	std::unordered_set<std::string> shadowedNames;
	for (Section *section = progress.patternReference->range().section(); section; section = section->parent) {
		for (const auto &[name, declaration] : section->explicitVariableDeclarations) {
			if (!explicitVariableDeclarationPrecedes(declaration, *progress.patternReference, progress.sourceElementIndex))
				continue;
			if (!shadowedNames.insert(name).second || name.find(' ') == std::string::npos)
				continue;
			std::vector<PatternElement> nameElements = getPatternElements(name);
			if (nameElements.empty() || progress.sourceElementIndex + nameElements.size() > sourceElements.size())
				continue;
			bool matches = true;
			for (size_t index = 0; index < nameElements.size(); index++) {
				const PatternElement &source = sourceElements[progress.sourceElementIndex + index];
				const PatternElement &expected = nameElements[index];
				if (source.type != expected.type || source.text != expected.text) {
					matches = false;
					break;
				}
			}
			if (matches)
				result.push_back({name, nameElements.size(), name.size()});
		}
	}
	std::sort(result.begin(), result.end(), [](const auto &left, const auto &right) {
		if (left.elementCount != right.elementCount)
			return left.elementCount < right.elementCount;
		return left.name < right.name;
	});
	return result;
}

static RemainingPatternElement
remainingPatternElement(const PatternReference *reference, size_t elementIndex, size_t charIndex) {
	if (!reference || elementIndex >= reference->patternElements.size())
		return {};
	const PatternElement &element = reference->patternElements[elementIndex];
	std::string_view text = element.text;
	if (charIndex > 0 && charIndex < text.size())
		text.remove_prefix(charIndex);
	return {true, element.type, text};
}

static std::optional<std::string> numericSourceSpelling(const PatternReference *reference, size_t sourceArgumentIndex) {
	if (!reference || !reference->expression || sourceArgumentIndex >= reference->expression->arguments.size())
		return std::nullopt;
	const Expression *argument = reference->expression->arguments[sourceArgumentIndex];
	if (!argument || argument->kind != Expression::Kind::Literal)
		return std::nullopt;
	const bool numeric = std::holds_alternative<std::int64_t>(argument->literalValue) ||
						 std::holds_alternative<MinimumSignedIntegerMagnitude>(argument->literalValue) ||
						 std::holds_alternative<double>(argument->literalValue);
	if (!numeric || !argument->range.line)
		return std::nullopt;
	return std::string(argument->range.subString);
}

template <typename T>
static std::vector<T> materializeSequence(const MatchSequence<T> &sequence, const std::vector<MatchSequenceNode<T>> &storage) {
	std::vector<T> result;
	result.reserve(sequence.size());
	for (size_t nodeIndex = sequence.last; nodeIndex != noMatchSequenceNode; nodeIndex = storage[nodeIndex].previous) {
		requireCompilerInvariant(nodeIndex < storage.size(), "matcher sequence contains an invalid node index");
		result.push_back(storage[nodeIndex].value);
	}
	std::reverse(result.begin(), result.end());
	return result;
}

static std::vector<PatternMatch> materializeSubMatches(
	const MatchSequence<const PatternMatch *> &sequence, const std::vector<MatchSequenceNode<const PatternMatch *>> &storage
) {
	std::vector<PatternMatch> result;
	result.reserve(sequence.size());
	for (size_t nodeIndex = sequence.last; nodeIndex != noMatchSequenceNode; nodeIndex = storage[nodeIndex].previous) {
		requireCompilerInvariant(nodeIndex < storage.size(), "matcher submatch sequence contains an invalid node index");
		const PatternMatch *match = storage[nodeIndex].value;
		requireCompilerInvariant(match != nullptr, "matcher submatch sequence contains a null match");
		result.push_back(*match);
	}
	std::reverse(result.begin(), result.end());
	return result;
}

template <typename T>
static void appendSequence(std::vector<MatchSequenceNode<T>> &storage, MatchSequence<T> &sequence, T value) {
	storage.push_back({std::move(value), sequence.last});
	sequence.last = storage.size() - 1;
	sequence.count++;
}

} // namespace

void collectMatchDependencies(const MatchControlState &state, MatchDependencies &dependencies) {
	dependencies.push_back({MatchDependency::Kind::Endpoint, state.currentNode, state.currentNode->endpointRevision, {}});
	if (!state.rootNode->argumentChild)
		dependencies.push_back({MatchDependency::Kind::ArgumentChild, state.rootNode, 0, {}});

	RemainingPatternElement element =
		remainingPatternElement(state.patternReference, state.sourceElementIndex, state.sourceCharIndex);
	if (!element.exists)
		return;

	if (!state.currentNode->argumentChild)
		dependencies.push_back({MatchDependency::Kind::ArgumentChild, state.currentNode, 0, {}});
	if (element.type == PatternElement::Type::Variable) {
		if (std::optional<std::string> numericSpelling =
				numericSourceSpelling(state.patternReference, state.sourceArgumentIndex);
			numericSpelling && !state.currentNode->literalChildren.contains(*numericSpelling))
			dependencies.push_back({MatchDependency::Kind::LiteralChild, state.currentNode, 0, *numericSpelling});
		return;
	}

	auto fullLiteral = state.currentNode->literalChildren.find(element.text);
	if (fullLiteral == state.currentNode->literalChildren.end())
		dependencies.push_back({MatchDependency::Kind::LiteralChild, state.currentNode, 0, std::string(element.text)});
	if (element.type != PatternElement::Type::Other || element.text.size() <= 1 ||
		fullLiteral != state.currentNode->literalChildren.end())
		return;

	for (size_t prefixLength = 1; prefixLength < element.text.size(); prefixLength++) {
		std::string_view prefix = element.text.substr(0, prefixLength);
		if (!state.currentNode->literalChildren.contains(prefix))
			dependencies.push_back({MatchDependency::Kind::LiteralChild, state.currentNode, 0, std::string(prefix)});
	}
}

void normalizeMatchDependencies(MatchDependencies &dependencies) {
	auto comesBefore = [](const MatchDependency &left, const MatchDependency &right) {
		if (left.node != right.node)
			return std::less<const PatternTreeNode *>{}(left.node, right.node);
		if (left.kind != right.kind)
			return left.kind < right.kind;
		if (left.literal != right.literal)
			return left.literal < right.literal;
		return left.endpointRevision < right.endpointRevision;
	};
	std::sort(dependencies.begin(), dependencies.end(), comesBefore);
	dependencies.erase(
		std::unique(
			dependencies.begin(), dependencies.end(),
			[](const MatchDependency &left, const MatchDependency &right) {
		return left.node == right.node && left.kind == right.kind && left.literal == right.literal &&
			   left.endpointRevision == right.endpointRevision;
	}
		),
		dependencies.end()
	);
}

size_t MatchControlStateHash::operator()(const MatchControlState &state) const {
	size_t hash = 0;
	combineMatchStateHash(hash, std::hash<const PatternTreeNode *>{}(state.rootNode));
	combineMatchStateHash(hash, std::hash<const PatternTreeNode *>{}(state.currentNode));
	combineMatchStateHash(hash, std::hash<const PatternReference *>{}(state.patternReference));
	combineMatchStateHash(hash, static_cast<size_t>(state.type));
	combineMatchStateHash(hash, state.acceptLiterals);
	combineMatchStateHash(hash, state.hasParents);
	combineMatchStateHash(hash, state.sourceElementIndex);
	combineMatchStateHash(hash, state.sourceCharIndex);
	combineMatchStateHash(hash, state.sourceArgumentIndex);
	combineMatchStateHash(hash, state.patternStartPos);
	combineMatchStateHash(hash, state.patternPos);
	combineMatchStateHash(hash, state.matchedArgumentIndex);
	return hash;
}

size_t MatchContinuationStateHash::operator()(const MatchContinuationState &state) const {
	size_t hash = MatchControlStateHash{}(state.control);
	combineMatchStateHash(hash, std::hash<const MatchParentAlternatives *>{}(state.parents));
	return hash;
}

size_t CompletedMatchStateHash::operator()(const CompletedMatchState &state) const {
	size_t hash = 0;
	combineMatchStateHash(hash, std::hash<const PatternTreeNode *>{}(state.currentNode));
	combineMatchStateHash(hash, state.sourceElementIndex);
	combineMatchStateHash(hash, state.sourceArgumentIndex);
	combineMatchStateHash(hash, state.patternStartPos);
	combineMatchStateHash(hash, state.patternPos);
	return hash;
}

CompletedMatchState CompletedMatchProgress::state() const {
	return {currentNode, sourceElementIndex, sourceArgumentIndex, patternStartPos, patternPos};
}

bool MatchParentAlternatives::addParent(const MatchProgress *parent) {
	if (!parent || !valueStates.insert(parent->continuationState()).second)
		return false;
	values.push_back(parent);
	return true;
}

bool MatchParentAlternatives::addCompletion(CompletedMatchProgress completion) {
	if (!completedSubmatchStates.insert(completion.state()).second)
		return false;
	completedSubmatches.push_back(std::move(completion));
	return true;
}

MatchProgress::MatchProgress(ParseContext *context, PatternReference *patternReference)
	: MatchProgress(context, patternReference, {}) {}

MatchProgress::MatchProgress(ParseContext *context, PatternReference *patternReference, MatchOptions options)
	: context(context), patternReference(patternReference), type(patternReference->patternType) {
	this->options = options;
	rootNode = context->patternTrees[(int)patternReference->patternType];
	currentNode = rootNode;
}

bool MatchProgress::isComplete(const std::vector<PatternDefinition *> &visibleDefinitions) const {
	return !parents && sourceElementIndex == patternReference->patternElements.size() && !visibleDefinitions.empty();
}

bool MatchProgress::isSubmatchComplete(const std::vector<PatternDefinition *> &visibleDefinitions) const {
	return parents && canBeSubmatch() && currentNode && !visibleDefinitions.empty();
}

std::vector<PatternDefinition *> MatchProgress::visibleDefinitions() const {
	std::vector<PatternDefinition *> result;
	if (!currentNode)
		return result;
	const lsp::SourceFile *sourceFile =
		patternReference && patternReference->range().line ? patternReference->range().line->sourceFile : nullptr;
	requireCompilerInvariant(sourceFile != nullptr, "pattern matching requires a source file");
	for (PatternDefinition *definition : currentNode->matchingDefinitions) {
		requireCompilerInvariant(definition != nullptr, "pattern tree endpoint contains a null definition");
		if (isPatternDefinitionVisibleFromSource(*definition, *sourceFile))
			result.push_back(definition);
	}
	return result;
}

MatchControlState MatchProgress::controlState() const {
	return {
		rootNode,
		currentNode,
		patternReference,
		type,
		options.acceptLiterals,
		parents != nullptr,
		sourceElementIndex,
		sourceCharIndex,
		sourceArgumentIndex,
		patternStartPos,
		patternPos,
		matchedArgumentIndex,
	};
}

MatchContinuationState MatchProgress::continuationState() const { return {controlState(), parents}; }

MatchStep MatchProgress::step(MatchStorage &storage, const std::vector<PatternDefinition *> &visibleDefinitions) {
	MatchStep result;
	std::vector<MatchProgress> &nextMatches = result.nextMatches;

	RemainingPatternElement element = remainingPatternElement(patternReference, sourceElementIndex, sourceCharIndex);
	bool hasSourceElement = element.exists;
	PatternElement::Type elementType = element.type;
	std::string_view elementText = element.text;
	auto fullLiteralMatch = hasSourceElement && elementType != PatternElement::Type::Variable
								? currentNode->literalChildren.find(elementText)
								: currentNode->literalChildren.end();

	// Lowest priority first. ParseContext::match() explores queue.back(), so later pushes run earlier.
	if (hasSourceElement && elementType == PatternElement::Type::Other && elementText.size() > 1) {
		if (fullLiteralMatch == currentNode->literalChildren.end()) {
			for (size_t prefixLength = 1; prefixLength < elementText.size(); prefixLength++) {
				std::string_view prefix = elementText.substr(0, prefixLength);
				auto prefixMatch = currentNode->literalChildren.find(prefix);
				if (prefixMatch == currentNode->literalChildren.end())
					continue;
				MatchProgress splitStep = *this;
				splitStep.currentNode = prefixMatch->second;
				storage.append(splitStep.match.nodesPassed, splitStep.currentNode);
				splitStep.sourceCharIndex += prefixLength;
				splitStep.patternPos += prefixLength;
				nextMatches.push_back(std::move(splitStep));
			}
		}
	}

	if (hasSourceElement && options.acceptLiterals && elementType == PatternElement::Type::Variable) {
		struct LiteralFallbackCandidate {
			PatternTreeNode *node{};
			size_t firstStartPos{std::numeric_limits<size_t>::max()};
		};
		std::vector<LiteralFallbackCandidate> literalFallbackChildren;
		literalFallbackChildren.reserve(currentNode->literalChildren.size());
		for (const auto &[ignoredText, child] : currentNode->literalChildren) {
			if (!child || child->type != PatternElement::Type::VariableLike)
				continue;
			literalFallbackChildren.push_back({child, firstDefinitionStartPos(child)});
		}
		std::sort(
			literalFallbackChildren.begin(), literalFallbackChildren.end(),
			[](const LiteralFallbackCandidate &left, const LiteralFallbackCandidate &right) {
			if (left.firstStartPos != right.firstStartPos)
				return left.firstStartPos < right.firstStartPos;
			return left.node->text < right.node->text;
		}
		);
		for (const LiteralFallbackCandidate &candidate : literalFallbackChildren) {
			PatternTreeNode *child = candidate.node;
			MatchProgress acceptedLiteralStep = *this;
			acceptedLiteralStep.currentNode = child;
			storage.append(acceptedLiteralStep.match.nodesPassed, child);
			storage.append(acceptedLiteralStep.match.acceptedLiterals, AcceptedLiteralMatch{child});
			acceptedLiteralStep.sourceElementIndex++;
			if (!patternReference->expression || sourceArgumentIndex >= patternReference->expression->arguments.size())
				continue;
			storage.append(
				acceptedLiteralStep.match.orderedArguments,
				{acceptedLiteralStep.matchedArgumentIndex, MatchedArgument::Kind::Expression,
				 patternReference->expression->arguments[sourceArgumentIndex], 0}
			);
			acceptedLiteralStep.sourceArgumentIndex++;
			acceptedLiteralStep.matchedArgumentIndex++;
			acceptedLiteralStep.patternPos += elementText.size();
			nextMatches.push_back(std::move(acceptedLiteralStep));
		}
	}

	if (!visibleDefinitions.empty() && canBeSubmatch()) {
		result.completedSubmatch = completedSubmatch(storage, visibleDefinitions);
		result.hasCompletedSubmatch = true;

		// Try extending as left operand of a new expression first (lower LIFO priority).
		// f.e: 'the result' in 'the result = 10', or '$ + $' in 'set $ to $ + $ dollars'
		if (canStartSubmatch() && rootNode->argumentChild) {
			MatchProgress clone = *this;
			clone.rootNode = rootNode;
			// advance past the argument slot — the completed sub-expression occupies it
			clone.currentNode = rootNode->argumentChild;
			clone.match = {};
			storage.append(clone.match.nodesPassed, clone.currentNode);
			clone.matchedArgumentIndex = 0;

			clone.type = SectionType::Function;
			nextMatches.push_back(resumeParent(storage, clone, result.completedSubmatch));
		}
		// Step up to parent match (higher LIFO priority — prefer returning to the
		// parent over speculatively extending into a new expression).
		if (parents) {
			for (auto parent = parents->values.rbegin(); parent != parents->values.rend(); parent++)
				nextMatches.push_back(resumeParent(storage, **parent, result.completedSubmatch));
		}
	}

	if (hasSourceElement) {
		// less priority: arguments
		if (currentNode->argumentChild) {

			auto pushArgumentCapture = [&]() -> void {
				if (elementType != PatternElement::Type::Variable && elementType != PatternElement::Type::VariableLike)
					return;
				MatchProgress substituteStep = *this;
				substituteStep.currentNode = currentNode->argumentChild;
				storage.append(substituteStep.match.nodesPassed, substituteStep.currentNode);
				substituteStep.sourceElementIndex++;
				if (elementType == PatternElement::Type::VariableLike) {
					size_t lineStart = patternReference->pattern.getLinePos(patternPos);
					size_t lineEnd = patternReference->pattern.getLinePos(patternPos + elementText.size());
					size_t variableIndex = substituteStep.match.discoveredVariables.size();
					storage.append(
						substituteStep.match.discoveredVariables, VariableMatch{std::string(elementText), lineStart, lineEnd}
					);
					storage.append(
						substituteStep.match.orderedArguments,
						{substituteStep.matchedArgumentIndex, MatchedArgument::Kind::Variable, nullptr, variableIndex}
					);
				} else {
					storage.append(
						substituteStep.match.orderedArguments,
						{substituteStep.matchedArgumentIndex, MatchedArgument::Kind::Expression,
						 patternReference->expression->arguments[sourceArgumentIndex], 0}
					);
					substituteStep.sourceArgumentIndex++;
				}
				substituteStep.matchedArgumentIndex++;
				substituteStep.patternPos += elementText.size();
				nextMatches.push_back(std::move(substituteStep));
			};

			auto pushExplicitMultiWordCaptures = [&]() {
				for (const ExplicitVariableCandidate &candidate : explicitMultiWordVariableCandidates(*this)) {
					MatchProgress substituteStep = *this;
					substituteStep.currentNode = currentNode->argumentChild;
					storage.append(substituteStep.match.nodesPassed, substituteStep.currentNode);
					substituteStep.sourceElementIndex += candidate.elementCount;
					size_t lineStart = patternReference->pattern.getLinePos(patternPos);
					size_t lineEnd = patternReference->pattern.getLinePos(patternPos + candidate.textLength);
					size_t variableIndex = substituteStep.match.discoveredVariables.size();
					storage.append(substituteStep.match.discoveredVariables, VariableMatch{candidate.name, lineStart, lineEnd});
					storage.append(
						substituteStep.match.orderedArguments,
						{substituteStep.matchedArgumentIndex, MatchedArgument::Kind::Variable, nullptr, variableIndex}
					);
					substituteStep.matchedArgumentIndex++;
					substituteStep.patternPos += candidate.textLength;
					nextMatches.push_back(std::move(substituteStep));
				}
			};

			auto pushSubmatch = [&]() {
				if (!canStartSubmatch())
					return;
				MatchProgress subMatch = *this;
				subMatch.currentNode = context->patternTrees[(int)SectionType::Function];
				subMatch.rootNode = subMatch.currentNode;
				subMatch.type = SectionType::Function;
				subMatch.patternStartPos = patternPos;
				subMatch.match = {};

				MatchProgress parentStep = *this;
				parentStep.currentNode = currentNode->argumentChild;
				storage.append(parentStep.match.nodesPassed, parentStep.currentNode);
				subMatch.parents = storage.createParentAlternatives();
				subMatch.parents->addParent(storage.storeParent(std::move(parentStep)));
				nextMatches.push_back(std::move(subMatch));
			};

			pushArgumentCapture();
			pushExplicitMultiWordCaptures();
			pushSubmatch();
		}
		// most priority: text match
		if (fullLiteralMatch != currentNode->literalChildren.end()) {
			MatchProgress elemStep = *this;
			elemStep.currentNode = fullLiteralMatch->second;
			storage.append(elemStep.match.nodesPassed, elemStep.currentNode);
			elemStep.sourceElementIndex++;
			elemStep.sourceCharIndex = 0;
			elemStep.patternPos += elementText.size();
			nextMatches.push_back(std::move(elemStep));
		}
		if (elementType == PatternElement::Type::Variable) {
			std::optional<std::string> numericSpelling = numericSourceSpelling(patternReference, sourceArgumentIndex);
			if (numericSpelling) {
				auto numericLiteralMatch = currentNode->literalChildren.find(*numericSpelling);
				if (numericLiteralMatch != currentNode->literalChildren.end()) {
					MatchProgress numericLiteralStep = *this;
					numericLiteralStep.currentNode = numericLiteralMatch->second;
					storage.append(numericLiteralStep.match.nodesPassed, numericLiteralStep.currentNode);
					numericLiteralStep.sourceElementIndex++;
					numericLiteralStep.sourceArgumentIndex++;
					numericLiteralStep.patternPos += elementText.size();
					nextMatches.push_back(std::move(numericLiteralStep));
				}
			}
		}
	}
	return result;
}

bool MatchProgress::canStartSubmatch() const {
	// prevent infinite recursion
	return type != SectionType::Function || currentNode != rootNode;
}

bool MatchProgress::canBeSubmatch() const { return type == SectionType::Function; }

CompletedMatchProgress
MatchProgress::completedSubmatch(MatchStorage &storage, const std::vector<PatternDefinition *> &visibleDefinitions) const {
	CompletedMatchProgress completed;
	completed.currentNode = currentNode;
	completed.match = storage.storeCompletedMatch(materializeMatch(storage, visibleDefinitions));
	completed.sourceElementIndex = sourceElementIndex;
	completed.sourceArgumentIndex = sourceArgumentIndex;
	completed.patternStartPos = patternStartPos;
	completed.patternPos = patternPos;
	return completed;
}

MatchProgress MatchProgress::resumeParent(
	MatchStorage &storage, const MatchProgress &parentProgress, const CompletedMatchProgress &submatch
) {
	requireCompilerInvariant(submatch.match != nullptr, "cannot resume a pattern match from a null submatch");
	MatchProgress resumed = parentProgress;
	size_t subMatchIndex = resumed.match.subMatches.size();
	storage.append(resumed.match.subMatches, submatch.match);
	storage.append(
		resumed.match.orderedArguments, {resumed.matchedArgumentIndex, MatchedArgument::Kind::SubMatch, nullptr, subMatchIndex}
	);
	// We already consumed the submatch's last source element.
	resumed.sourceElementIndex = submatch.sourceElementIndex;
	resumed.sourceArgumentIndex = submatch.sourceArgumentIndex;
	resumed.matchedArgumentIndex++;
	resumed.patternPos = submatch.patternPos;
	return resumed;
}

PatternMatch
MatchProgress::materializeMatch(const MatchStorage &storage, const std::vector<PatternDefinition *> &visibleDefinitions) const {
	PatternMatch result{};
	result.matchedEndNode = currentNode;
	result.matchingDefinitions = visibleDefinitions;
	result.lineStartPos = patternReference->pattern.getLinePos(patternStartPos);
	result.lineEndPos = patternReference->pattern.getLinePos(patternPos);
	result.nodesPassed = materializeSequence(match.nodesPassed, storage.matchedNodes);
	result.discoveredVariables = materializeSequence(match.discoveredVariables, storage.matchedVariables);
	result.acceptedLiterals = materializeSequence(match.acceptedLiterals, storage.acceptedLiterals);
	result.subMatches = materializeSubMatches(match.subMatches, storage.subMatches);
	result.orderedArguments = materializeSequence(match.orderedArguments, storage.orderedArguments);
	return result;
}

std::string MatchProgress::toString() const {
	const std::string currentConsumed = consumedSourcePrefix(patternReference, sourceElementIndex, sourceCharIndex);
	if (!parents || parents->values.empty())
		return currentConsumed;

	const MatchProgress &parent = *parents->values.front();
	const std::string parentRendered = parent.toString();
	const std::string parentConsumed =
		consumedSourcePrefix(parent.patternReference, parent.sourceElementIndex, parent.sourceCharIndex);
	std::string submatchConsumed = currentConsumed;
	if (currentConsumed.size() >= parentConsumed.size() &&
		currentConsumed.compare(0, parentConsumed.size(), parentConsumed) == 0)
		submatchConsumed = currentConsumed.substr(parentConsumed.size());
	return parentRendered + "(" + submatchConsumed;
}

const MatchProgress *MatchStorage::storeParent(MatchProgress progress) {
	parentProgresses.push_back(std::move(progress));
	return &parentProgresses.back();
}

MatchParentAlternatives *MatchStorage::createParentAlternatives() {
	parentAlternatives.emplace_back();
	return &parentAlternatives.back();
}

const PatternMatch *MatchStorage::storeCompletedMatch(PatternMatch match) {
	completedMatches.push_back(std::move(match));
	return &completedMatches.back();
}

void MatchStorage::append(MatchSequence<PatternTreeNode *> &sequence, PatternTreeNode *value) {
	appendSequence(matchedNodes, sequence, value);
}

void MatchStorage::append(MatchSequence<VariableMatch> &sequence, VariableMatch value) {
	appendSequence(matchedVariables, sequence, std::move(value));
}

void MatchStorage::append(MatchSequence<AcceptedLiteralMatch> &sequence, AcceptedLiteralMatch value) {
	appendSequence(acceptedLiterals, sequence, value);
}

void MatchStorage::append(MatchSequence<const PatternMatch *> &sequence, const PatternMatch *value) {
	appendSequence(subMatches, sequence, value);
}

void MatchStorage::append(MatchSequence<MatchedArgument> &sequence, MatchedArgument value) {
	appendSequence(orderedArguments, sequence, value);
}
