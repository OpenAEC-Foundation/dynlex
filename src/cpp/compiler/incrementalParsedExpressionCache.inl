constexpr std::string_view parsedExpressionCacheHeader = "DynLex parsed expressions v5\n";

void appendParseLength(std::string &output, std::uint64_t value) {
	do {
		unsigned char byte = static_cast<unsigned char>(value & 0x7f);
		value >>= 7;
		output.push_back(static_cast<char>(byte | (value ? 0x80 : 0)));
	} while (value);
}

std::uint64_t readParseLength(std::string_view input, size_t &position) {
	std::uint64_t value = 0;
	for (unsigned index = 0; index < 10; index++) {
		if (position == input.size())
			throw std::runtime_error("truncated parsed expression cache length");
		unsigned char byte = static_cast<unsigned char>(input[position++]);
		if (index == 9 && byte > 1)
			throw std::runtime_error("invalid parsed expression cache length");
		value |= static_cast<std::uint64_t>(byte & 0x7f) << (index * 7);
		if (!(byte & 0x80))
			return value;
	}
	throw std::runtime_error("invalid parsed expression cache length");
}

void appendParseString(std::string &output, std::string_view value) {
	appendParseLength(output, value.size());
	output.append(value);
}

std::string_view readParseString(std::string_view input, size_t &position) {
	std::uint64_t length = readParseLength(input, position);
	if (length > input.size() - position)
		throw std::runtime_error("truncated parsed expression cache string");
	std::string_view value = input.substr(position, static_cast<size_t>(length));
	position += static_cast<size_t>(length);
	return value;
}

void appendParseByte(std::string &output, unsigned char value) { output.push_back(static_cast<char>(value)); }

unsigned char readParseByte(std::string_view input, size_t &position) {
	if (position == input.size())
		throw std::runtime_error("truncated parsed expression cache byte");
	return static_cast<unsigned char>(input[position++]);
}

void appendParseWord(std::string &output, std::uint64_t value) {
	for (unsigned index = 0; index < 8; index++) {
		output.push_back(static_cast<char>(value & 0xff));
		value >>= 8;
	}
}

std::uint64_t readParseWord(std::string_view input, size_t &position) {
	if (input.size() - position < 8)
		throw std::runtime_error("truncated parsed expression cache word");
	std::uint64_t value = 0;
	for (unsigned index = 0; index < 8; index++)
		value |= static_cast<std::uint64_t>(static_cast<unsigned char>(input[position++])) << (index * 8);
	return value;
}

void appendParseRange(std::string &output, Range root, Range range) {
	requireCompilerInvariant(
		range.line == root.line && range.start() >= root.start() && range.end() <= root.end(),
		"parsed expression range escapes its root"
	);
	appendParseLength(output, range.start() - root.start());
	appendParseLength(output, range.end() - root.start());
}

Range readParseRange(std::string_view input, size_t &position, Range root) {
	std::uint64_t start = readParseLength(input, position);
	std::uint64_t end = readParseLength(input, position);
	if (end < start || end > root.subString.size())
		throw std::runtime_error("invalid parsed expression cache range");
	return root.subRange(static_cast<int>(start), static_cast<int>(end));
}

void appendParsedExpression(std::string &output, Range root, const Expression &expression) {
	appendParseByte(output, static_cast<unsigned char>(expression.kind));
	appendParseRange(output, root, expression.range);
	appendParseByte(output, expression.isExplicitGroup);
	switch (expression.kind) {
	case Expression::Kind::Literal:
		appendParseByte(output, static_cast<unsigned char>(expression.literalValue.index()));
		std::visit([&](const auto &value) {
			using Value = std::decay_t<decltype(value)>;
			if constexpr (std::is_same_v<Value, std::int64_t>)
				appendParseWord(output, std::bit_cast<std::uint64_t>(value));
			else if constexpr (std::is_same_v<Value, std::uint64_t>)
				appendParseWord(output, value);
			else if constexpr (std::is_same_v<Value, double>)
				appendParseWord(output, std::bit_cast<std::uint64_t>(value));
			else if constexpr (std::is_same_v<Value, std::string>)
				appendParseString(output, value);
			else if constexpr (std::is_same_v<Value, WideInteger>) {
				appendParseLength(output, value.bits.getBitWidth());
				appendParseByte(output, value.isUnsigned);
				appendParseString(output, value.toString(16));
			}
		}, expression.literalValue);
		break;
	case Expression::Kind::Variable: {
		const VariableReference *variable = expression.variable;
		requireCompilerInvariant(variable != nullptr, "parsed variable expression lost its reference");
		appendParseRange(output, root, variable->range);
		appendParseString(output, variable->name);
		appendParseString(output, variable->declaredTypeConstraintName);
		if (!variable->declaredTypeConstraintName.empty())
			appendParseRange(output, root, variable->declaredTypeConstraintRange);
		break;
	}
	case Expression::Kind::Pending: {
		const PatternReference *reference = expression.patternReference;
		requireCompilerInvariant(reference != nullptr, "parsed pending expression lost its reference");
		appendParseByte(output, static_cast<unsigned char>(reference->patternType));
		appendParseString(output, reference->pattern.text);
		appendParseLength(output, reference->pattern.keyframes.size());
		for (const TransformedPattern::KeyFrame &frame : reference->pattern.keyframes) {
			appendParseLength(output, frame.patternPos);
			appendParseLength(output, frame.linePos);
		}
		break;
	}
	case Expression::Kind::IntrinsicCall:
		appendParseString(output, expression.intrinsicName);
		break;
	case Expression::Kind::ArrayLiteral:
		break;
	default:
		crashCompilerBug("incremental parse cache encountered a resolved expression");
	}
	appendParseLength(output, expression.arguments.size());
	for (const Expression *argument : expression.arguments)
		appendParsedExpression(output, root, *argument);
}

struct ParsedExpressionReplay {
	std::vector<PatternReference *> references;
	std::vector<VariableReference *> variables;
	std::vector<Expression *> expressions;
	size_t next = 0;
};

Expression *restoreParsedExpression(
	std::string_view input, size_t &position, ParseContext &context, Section *section, Range root,
	bool registerPatternReferences, ParsedExpressionReplay *replay = nullptr
) {
	size_t node = replay ? replay->next++ : 0;
	auto kind = static_cast<Expression::Kind>(readParseByte(input, position));
	Range range = readParseRange(input, position, root);
	auto *expression = new Expression();
	if (replay)
		replay->expressions.at(node) = expression;
	expression->kind = kind;
	expression->range = range;
	expression->isExplicitGroup = readParseByte(input, position) != 0;
	switch (kind) {
	case Expression::Kind::Literal:
		switch (readParseByte(input, position)) {
		case 0:
			break;
		case 1:
			expression->literalValue = std::bit_cast<std::int64_t>(readParseWord(input, position));
			break;
		case 2:
			expression->literalValue = readParseWord(input, position);
			break;
		case 3:
			expression->literalValue =
				MinimumSignedIntegerMagnitude{std::make_shared<const MinimumSignedIntegerMagnitudeIdentity>()};
			break;
		case 4:
			expression->literalValue = std::bit_cast<double>(readParseWord(input, position));
			break;
		case 5:
			expression->literalValue = std::string(readParseString(input, position));
			break;
		case 6: {
			unsigned width = readParseLength(input, position);
			bool isUnsigned = readParseByte(input, position) != 0;
			expression->literalValue = WideInteger{llvm::APInt(width, readParseString(input, position), 16), isUnsigned};
			break;
		}
		default:
			throw std::runtime_error("invalid parsed expression literal kind");
		}
		break;
	case Expression::Kind::Variable: {
		Range nameRange = readParseRange(input, position, root);
		std::string name(readParseString(input, position));
		std::string typeName(readParseString(input, position));
		VariableReference *variable = replay ? replay->variables.at(node) : context.createVariableReference(nameRange, name);
		requireCompilerInvariant(variable, "parsed variable metadata lost its binding");
		variable->declaredTypeConstraintName = std::move(typeName);
		if (!variable->declaredTypeConstraintName.empty())
			variable->declaredTypeConstraintRange = readParseRange(input, position, root);
		if (registerPatternReferences) {
			if (variable->declaredTypeConstraintName.empty())
				section->registerExplicitVariableName(name, nameRange);
			context.pendingExplicitVariableReferences.push_back(variable);
		} else if (auto definition = section->variableDefinitions.find(name);
				   !replay && definition != section->variableDefinitions.end()) {
			variable->definition = normalizeBindingReference(definition->second);
		}
		if (!replay)
			context.addSourceToken(nameRange, ParseContext::SourceTokenKind::Variable);
		expression->variable = variable;
		break;
	}
	case Expression::Kind::Pending: {
		auto patternType = static_cast<SectionType>(readParseByte(input, position));
		if (patternType >= SectionType::Count)
			throw std::runtime_error("invalid parsed expression pattern type");
		auto *reference = replay ? replay->references.at(node) : new PatternReference(expression, patternType);
		requireCompilerInvariant(reference, "parsed expression metadata lost its pattern reference");
		reference->matchingScope = section;
		reference->pattern.text = std::string(readParseString(input, position));
		std::uint64_t frames = readParseLength(input, position);
		if (frames > input.size() - position)
			throw std::runtime_error("invalid parsed expression keyframe count");
		reference->pattern.keyframes.clear();
		for (std::uint64_t index = 0; index < frames; index++)
			reference->pattern.keyframes.push_back({readParseLength(input, position), readParseLength(input, position)});
		expression->patternReference = reference;
		break;
	}
	case Expression::Kind::IntrinsicCall:
		expression->intrinsicName = std::string(readParseString(input, position));
		break;
	case Expression::Kind::ArrayLiteral:
		break;
	default:
		throw std::runtime_error("invalid parsed expression kind");
	}
	std::uint64_t arguments = readParseLength(input, position);
	if (arguments > input.size() - position)
		throw std::runtime_error("invalid parsed expression argument count");
	for (std::uint64_t index = 0; index < arguments; index++)
		expression->arguments.push_back(
			restoreParsedExpression(input, position, context, section, root, registerPatternReferences, replay)
		);
	if (registerPatternReferences && expression->patternReference)
		section->addPatternReference(expression->patternReference);
	return expression;
}

#include "incrementalParsedDefinitionCache.inl"
#include "incrementalSourceSyntax.inl"

class IncrementalParsedExpressionCache {
  public:
	enum class SnapshotScope { Complete, Partial };
	IncrementalParsedExpressionCache(ParseContext &context, std::string_view previous) : context(context) {
		if (previous.empty())
			return;
		if (!previous.starts_with(parsedExpressionCacheHeader))
			throw std::runtime_error("invalid parsed expression cache header");
		size_t position = parsedExpressionCacheHeader.size();
		while (position < previous.size()) {
			if (previous.size() - position < 65)
				throw std::runtime_error("truncated parsed expression cache key");
			std::string_view key = previous.substr(position, 65);
			if (key.front() != 'R' && key.front() != 'T' && key.front() != 'D' && key.front() != 'S' && key.front() != 'P')
				throw std::runtime_error("invalid parsed expression cache key kind");
			position += 65;
			std::uint64_t length = readParseLength(previous, position);
			if (length > previous.size() - position)
				throw std::runtime_error("truncated parsed expression cache value");
			auto [_, inserted] = prior.emplace(key, previous.substr(position, static_cast<size_t>(length)));
			if (!inserted)
				throw std::runtime_error("duplicate parsed expression cache key");
			position += static_cast<size_t>(length);
		}
	}

	Expression *reuse(Section *section, Range range, SectionType patternType, bool registerPatternReferences) {
		recordBodyAssembly(section, registerPatternReferences);
		std::string key = expressionKey(range, patternType, registerPatternReferences);
		if (!registerPatternReferences)
			hasCurrentTemporaryEntries = true;
		std::string_view payload;
		if (auto found = current.find(key); found != current.end()) {
			payload = std::visit([](const auto &value) -> std::string_view {
				return value;
			}, found->second);
		} else if (auto found = prior.find(key); found != prior.end()) {
			payload = found->second;
			current.emplace(key, payload);
			priorHits++;
			if (!registerPatternReferences)
				temporaryHits++;
		} else {
			misses++;
			return nullptr;
		}
		size_t position = 0;
		std::string_view syntax = readParseString(payload, position);
		size_t syntaxPosition = 0;
		Expression *expression =
			restoreParsedExpression(syntax, syntaxPosition, context, section, range, registerPatternReferences);
		if (syntaxPosition != syntax.size())
			throw std::runtime_error("trailing parsed expression cache data");
		return expression;
	}

	void record(Range range, SectionType patternType, bool registerPatternReferences, const Expression *expression) {
		if (!registerPatternReferences)
			hasCurrentTemporaryEntries = true;
		std::string syntax, payload;
		appendParsedExpression(syntax, range, *expression);
		appendParseString(payload, syntax);
		appendSourceSyntaxOutline(payload, range, *expression);
		current.insert_or_assign(expressionKey(range, patternType, registerPatternReferences), std::move(payload));
	}

	bool defer(Section *section, CodeLine *line, SectionType patternType) {
		Section *owner = sourceSyntaxOwner(section);
		if (!owner || context.compilationStage != ParseContext::CompilationStage::ImportedFiles)
			return false;
		Range range(line, line->patternText);
		std::string key = expressionKey(range, patternType, true);
		std::string_view payload;
		if (auto found = current.find(key); found != current.end())
			payload = std::visit([](const auto &entry) -> std::string_view {
				return entry;
			}, found->second);
		else if (auto found = prior.find(key); found != prior.end()) {
			payload = found->second;
			current.emplace(key, payload);
			priorHits++;
		} else
			return false;
		SectionAnalysisWork *analysis =
			context.sourceTransformHooks->sectionAnalysisEnabled ? &context.sourceTransformHooks->sectionAnalysis : nullptr;
		SectionAnalysisTimer timer(analysis ? &analysis->outlineRestoreMilliseconds : nullptr);
		auto product = std::make_unique<CachedSourceSyntax>(context, section, owner, range, payload, bodyWork, analysis);
		line->expression.defer(*product);
		context.sourceSyntaxProducts.push_back(std::move(product));
		bodyWork->deferred.insert(owner);
		return true;
	}

	void retainTemporary(Range range, SectionType patternType) {
		std::string key = expressionKey(range, patternType, false);
		if (current.contains(key))
			return;
		if (auto found = prior.find(key); found != prior.end()) {
			current.emplace(std::move(key), found->second);
			hasCurrentTemporaryEntries = true;
		}
	}

	bool reuseDefinition(Range range, std::vector<DefinitionPatternElement> &elements) {
		std::string key = definitionKey(range);
		std::string_view payload;
		if (auto found = current.find(key); found != current.end()) {
			payload = std::visit([](const auto &value) -> std::string_view {
				return value;
			}, found->second);
		} else if (auto found = prior.find(key); found != prior.end()) {
			payload = found->second;
			current.emplace(key, payload);
			definitionHits++;
		} else {
			definitionMisses++;
			return false;
		}
		size_t position = 0;
		std::uint64_t count = readParseLength(payload, position);
		if (count > payload.size() - position)
			throw std::runtime_error("invalid parsed definition element count");
		for (std::uint64_t index = 0; index < count; index++)
			elements.push_back(readParsedDefinitionElement(payload, position, range));
		if (position != payload.size())
			throw std::runtime_error("trailing parsed definition cache data");
		return true;
	}

	void recordDefinition(Range range, const std::vector<DefinitionPatternElement> &elements) {
		std::string payload;
		appendParseLength(payload, elements.size());
		for (const DefinitionPatternElement &element : elements)
			appendParsedDefinitionElement(payload, range, element);
		current.insert_or_assign(definitionKey(range), std::move(payload));
	}

	std::optional<std::string_view> priorSourceProduct(const std::string &key) const {
		auto found = prior.find(key);
		if (found == prior.end())
			return std::nullopt;
		if (found->second.size() < 64)
			throw std::runtime_error("truncated source product input fingerprint");
		return found->second;
	}
	void retainSourceProduct(const std::string &key, std::string_view value) { current.emplace(key, value); }
	std::optional<std::string_view> sourceProduct(const std::string &key, std::string_view inputs) {
		auto value = priorSourceProduct(key);
		if (!value || value->substr(0, 64) != inputs)
			return std::nullopt;
		retainSourceProduct(key, *value);
		return value->substr(64);
	}
	void recordSourceProduct(std::string key, std::string inputs, std::string payload) {
		inputs += payload;
		current.insert_or_assign(std::move(key), std::move(inputs));
	}

	std::string snapshot(SnapshotScope scope) const {
		std::string output(parsedExpressionCacheHeader);
		auto append = [&](std::string_view key, std::string_view payload) {
			output.append(key);
			appendParseLength(output, payload.size());
			output.append(payload);
		};
		for (const auto &[key, value] : current) {
			std::string_view payload = std::visit([](const auto &entry) -> std::string_view {
				return entry;
			}, value);
			append(key, payload);
		}
		std::vector<std::string_view> retained;
		for (const auto &[key, _] : prior)
			if (!current.contains(std::string(key)) &&
				(scope == SnapshotScope::Partial || (key.front() == 'T' && !hasCurrentTemporaryEntries)))
				retained.push_back(key);
		std::ranges::sort(retained);
		for (std::string_view key : retained)
			append(key, prior.at(key));
		return output;
	}

	size_t hits() const { return priorHits; }
	size_t temporaryHitCount() const { return temporaryHits; }
	size_t missCount() const { return misses; }
	size_t definitionHitCount() const { return definitionHits; }
	size_t definitionMissCount() const { return definitionMisses; }
	size_t bodyAssemblyCount() const { return bodyWork->parsed.size(); }
	size_t bodyRestorationCount() const { return bodyWork->restored.size(); }
	size_t bodySkippedCount() const {
		return std::ranges::count_if(bodyWork->deferred, [&](const Section *owner) {
			return !bodyWork->parsed.contains(owner) && !bodyWork->restored.contains(owner);
		});
	}

  private:
	void recordBodyAssembly(Section *scope, bool registerPatternReferences) {
		if (!registerPatternReferences || context.compilationStage != ParseContext::CompilationStage::ImportedFiles)
			return;
		if (Section *owner = sourceSyntaxOwner(scope))
			bodyWork->parsed.insert(owner);
	}

	static std::string expressionKey(Range range, SectionType patternType, bool registerPatternReferences) {
		std::string source;
		source.reserve(range.subString.size() + 3);
		source.push_back(registerPatternReferences ? 'R' : 'T');
		source.push_back(static_cast<char>(patternType));
		source.append(range.subString);
		return std::string(1, registerPatternReferences ? 'R' : 'T') + digest(source);
	}
	static std::string definitionKey(Range range) { return "D" + digest("definition:" + std::string(range.subString)); }

	ParseContext &context;
	std::unordered_map<std::string_view, std::string_view> prior;
	std::map<std::string, std::variant<std::string, std::string_view>> current;
	std::shared_ptr<SourceSyntaxWork> bodyWork = std::make_shared<SourceSyntaxWork>();
	size_t priorHits = 0;
	size_t temporaryHits = 0;
	bool hasCurrentTemporaryEntries = false;
	size_t misses = 0;
	size_t definitionHits = 0;
	size_t definitionMisses = 0;
};
