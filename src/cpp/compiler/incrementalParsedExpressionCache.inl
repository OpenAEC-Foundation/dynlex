constexpr std::string_view parsedExpressionCacheHeader = "DynLex parsed expressions v1\n";

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

Expression *
restoreParsedExpression(std::string_view input, size_t &position, ParseContext &context, Section *section, Range root) {
	auto kind = static_cast<Expression::Kind>(readParseByte(input, position));
	Range range = readParseRange(input, position, root);
	auto *expression = new Expression();
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
		default:
			throw std::runtime_error("invalid parsed expression literal kind");
		}
		break;
	case Expression::Kind::Variable: {
		Range nameRange = readParseRange(input, position, root);
		std::string name(readParseString(input, position));
		std::string typeName(readParseString(input, position));
		VariableReference *variable = context.createVariableReference(nameRange, name);
		variable->declaredTypeConstraintName = std::move(typeName);
		if (!variable->declaredTypeConstraintName.empty())
			variable->declaredTypeConstraintRange = readParseRange(input, position, root);
		if (variable->declaredTypeConstraintName.empty())
			section->registerExplicitVariableName(name, nameRange);
		context.pendingExplicitVariableReferences.push_back(variable);
		context.addSourceToken(nameRange, ParseContext::SourceTokenKind::Variable);
		expression->variable = variable;
		break;
	}
	case Expression::Kind::Pending: {
		auto patternType = static_cast<SectionType>(readParseByte(input, position));
		if (patternType >= SectionType::Count)
			throw std::runtime_error("invalid parsed expression pattern type");
		auto *reference = new PatternReference(expression, patternType);
		reference->matchingScope = section;
		reference->pattern.text = std::string(readParseString(input, position));
		std::uint64_t frames = readParseLength(input, position);
		if (frames > input.size() - position)
			throw std::runtime_error("invalid parsed expression keyframe count");
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
		expression->arguments.push_back(restoreParsedExpression(input, position, context, section, root));
	if (expression->patternReference)
		section->addPatternReference(expression->patternReference);
	return expression;
}

#include "incrementalParsedDefinitionCache.inl"

class IncrementalParsedExpressionCache {
  public:
	IncrementalParsedExpressionCache(ParseContext &context, std::string_view previous) : context(context) {
		if (previous.empty())
			return;
		if (!previous.starts_with(parsedExpressionCacheHeader))
			throw std::runtime_error("invalid parsed expression cache header");
		size_t position = parsedExpressionCacheHeader.size();
		while (position < previous.size()) {
			if (previous.size() - position < 64)
				throw std::runtime_error("truncated parsed expression cache key");
			std::string_view key = previous.substr(position, 64);
			position += 64;
			std::uint64_t length = readParseLength(previous, position);
			if (length > previous.size() - position)
				throw std::runtime_error("truncated parsed expression cache value");
			auto [_, inserted] = prior.emplace(key, previous.substr(position, static_cast<size_t>(length)));
			if (!inserted)
				throw std::runtime_error("duplicate parsed expression cache key");
			position += static_cast<size_t>(length);
		}
	}

	Expression *reuse(Section *section, Range range, SectionType patternType) {
		std::string key = expressionKey(range, patternType);
		std::string_view payload;
		if (auto found = current.find(key); found != current.end()) {
			payload = std::visit([](const auto &value) -> std::string_view {
				return value;
			}, found->second);
		} else if (auto found = prior.find(key); found != prior.end()) {
			payload = found->second;
			current.emplace(key, payload);
			priorHits++;
		} else {
			misses++;
			return nullptr;
		}
		size_t position = 0;
		Expression *expression = restoreParsedExpression(payload, position, context, section, range);
		if (position != payload.size())
			throw std::runtime_error("trailing parsed expression cache data");
		return expression;
	}

	void record(Range range, SectionType patternType, const Expression *expression) {
		std::string payload;
		appendParsedExpression(payload, range, *expression);
		current.insert_or_assign(expressionKey(range, patternType), std::move(payload));
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

	std::string snapshot() const {
		std::string output(parsedExpressionCacheHeader);
		for (const auto &[key, value] : current) {
			output += key;
			std::string_view payload = std::visit([](const auto &entry) -> std::string_view {
				return entry;
			}, value);
			appendParseLength(output, payload.size());
			output.append(payload);
		}
		return output;
	}

	size_t hits() const { return priorHits; }
	size_t missCount() const { return misses; }
	size_t definitionHitCount() const { return definitionHits; }
	size_t definitionMissCount() const { return definitionMisses; }

  private:
	static std::string expressionKey(Range range, SectionType patternType) {
		std::string source;
		source.reserve(range.subString.size() + 2);
		source.push_back(static_cast<char>(patternType));
		source.append(range.subString);
		return digest(source);
	}
	static std::string definitionKey(Range range) { return digest("definition:" + std::string(range.subString)); }

	ParseContext &context;
	std::unordered_map<std::string_view, std::string_view> prior;
	std::map<std::string, std::variant<std::string, std::string_view>> current;
	size_t priorHits = 0;
	size_t misses = 0;
	size_t definitionHits = 0;
	size_t definitionMisses = 0;
};
