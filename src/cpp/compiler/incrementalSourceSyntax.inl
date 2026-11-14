struct SourceSyntaxWork {
	std::unordered_set<const Section *> parsed, deferred, restored;
};

Section *sourceSyntaxOwner(Section *scope) {
	for (; scope && scope->parent; scope = scope->parent) {
		Section *owner = scope->parent;
		if (owner->patternDefinitions.empty())
			continue;
		return owner->type == SectionType::Function && Section::isDefinitionBodySectionType(scope->type) ? owner : nullptr;
	}
	return nullptr;
}

void appendSourceSyntaxOutline(std::string &output, Range root, const Expression &expression) {
	struct Node {
		const Expression *expression;
		std::vector<size_t> children;
	};
	std::vector<Node> nodes;
	std::vector<size_t> registration;
	std::function<size_t(const Expression &)> collect = [&](const Expression &expression) {
		size_t id = nodes.size();
		nodes.push_back({&expression, {}});
		for (const Expression *argument : expression.arguments) {
			size_t child = collect(*argument);
			nodes.at(id).children.push_back(child);
		}
		if (expression.patternReference)
			registration.push_back(id);
		return id;
	};
	collect(expression);
	appendParseLength(output, nodes.size());
	for (const Node &node : nodes) {
		const Expression &source = *node.expression;
		appendParseRange(output, root, source.range);
		appendParseByte(output, numericSourceExpressionSpelling(source).has_value());
		appendParseLength(output, node.children.size());
		for (size_t child : node.children)
			appendParseLength(output, child);
		unsigned kind = source.kind == Expression::Kind::Pending ? 1 : source.kind == Expression::Kind::Variable ? 2 : 0;
		appendParseByte(output, kind);
		if (kind == 1) {
			const PatternReference &reference = *source.patternReference;
			appendParseByte(output, static_cast<unsigned char>(reference.patternType));
			appendParseString(output, reference.pattern.text);
			appendParseLength(output, reference.pattern.keyframes.size());
			for (const auto &frame : reference.pattern.keyframes) {
				appendParseLength(output, frame.patternPos);
				appendParseLength(output, frame.linePos);
			}
		} else if (kind == 2) {
			const VariableReference &variable = *source.variable;
			appendParseRange(output, root, variable.range);
			appendParseString(output, variable.name);
			appendParseString(output, variable.declaredTypeConstraintName);
			if (!variable.declaredTypeConstraintName.empty())
				appendParseRange(output, root, variable.declaredTypeConstraintRange);
		}
	}
	appendParseLength(output, registration.size());
	for (size_t id : registration)
		appendParseLength(output, id);
}

// The bytecode and reference outline belong to the parse context, rather than a
// temporary cache reader. Inference can demand a body after cache hooks detach.
class CachedSourceSyntax : public BodySyntaxProduct, public DeferredExpressionSource {
  public:
	CachedSourceSyntax(
		ParseContext &context, Section *section, Section *owner, Range root, std::string_view payload,
		std::shared_ptr<SourceSyntaxWork> work
	)
		: context(context), owner(owner), root(root), payload(payload), work(std::move(work)) {
		size_t position = 0;
		syntax = readParseString(this->payload, position);
		size_t count = readParseLength(this->payload, position);
		if (!count || count > this->payload.size())
			throw std::runtime_error("invalid source syntax outline size");
		nodes.resize(count);
		replay.references.resize(count);
		replay.variables.resize(count);
		replay.expressions.resize(count);
		for (size_t id = 0; id < count; id++) {
			Node &node = nodes.at(id);
			node.range = readParseRange(this->payload, position, root);
			node.numeric = readParseByte(this->payload, position);
			size_t arguments = readParseLength(this->payload, position);
			if (arguments > count)
				throw std::runtime_error("invalid source syntax argument count");
			for (size_t index = 0; index < arguments; index++) {
				size_t child = readParseLength(this->payload, position);
				if (child <= id || child >= count)
					throw std::runtime_error("invalid source syntax child identity");
				node.arguments.push_back(child);
			}
			switch (readParseByte(this->payload, position)) {
			case 0:
				break;
			case 1: {
				auto type = static_cast<SectionType>(readParseByte(this->payload, position));
				if (type >= SectionType::Count)
					throw std::runtime_error("invalid source syntax pattern type");
				auto *reference = new PatternReference(SourceExpressionSite(*this, id), type);
				reference->matchingScope = section;
				reference->pattern.text = readParseString(this->payload, position);
				size_t frames = readParseLength(this->payload, position);
				if (frames > this->payload.size() - position)
					throw std::runtime_error("invalid source syntax keyframe count");
				for (size_t index = 0; index < frames; index++)
					reference->pattern.keyframes.push_back(
						{readParseLength(this->payload, position), readParseLength(this->payload, position)}
					);
				replay.references.at(id) = reference;
				break;
			}
			case 2: {
				Range nameRange = readParseRange(this->payload, position, root);
				std::string name(readParseString(this->payload, position));
				auto *variable = context.createVariableReference(nameRange, name);
				variable->declaredTypeConstraintName = readParseString(this->payload, position);
				if (!variable->declaredTypeConstraintName.empty())
					variable->declaredTypeConstraintRange = readParseRange(this->payload, position, root);
				else
					section->registerExplicitVariableName(name, nameRange);
				context.pendingExplicitVariableReferences.push_back(variable);
				context.addSourceToken(nameRange, ParseContext::SourceTokenKind::Variable);
				replay.variables.at(id) = variable;
				break;
			}
			default:
				throw std::runtime_error("invalid source syntax outline kind");
			}
		}
		size_t references = readParseLength(this->payload, position);
		if (references > count)
			throw std::runtime_error("invalid source syntax reference count");
		for (size_t index = 0; index < references; index++) {
			size_t id = readParseLength(this->payload, position);
			PatternReference *reference = replay.references.at(id);
			requireCompilerInvariant(reference, "source syntax registration lost its reference");
			section->addPatternReference(reference);
		}
		if (position != this->payload.size())
			throw std::runtime_error("trailing source syntax outline data");
	}

	void demand() override {
		if (materialized)
			return;
		requireCompilerInvariant(!materializing, "recursive source syntax demand before publication");
		if (context.demandBodyResolution)
			context.demandBodyResolution(owner);
		materializing = true;
		size_t position = 0;
		Expression *expression = restoreParsedExpression(syntax, position, context, root.section(), root, false, &replay);
		requireCompilerInvariant(
			position == syntax.size() && replay.next == nodes.size(), "source syntax replay lost its node identities"
		);
		root.line->expression.publish(*expression);
		if (context.sourceSyntaxResolutionComplete)
			expandExpression(expression, root.section());
		materialized = true;
		materializing = false;
		root.line->expression.finish();
		work->restored.insert(owner);
		if (context.sourceExpressionPublished)
			context.sourceExpressionPublished(root.line, expression);
	}
	bool syntaxMaterialized() const override { return materialized; }
	PatternReference *rootReference() const override { return replay.references.at(0); }
	const Section &bodyOwner() const override { return *owner; }
	Expression *expression(size_t node) override {
		if (!replay.expressions.at(node))
			demand();
		return replay.expressions.at(node);
	}
	Expression *materializedExpression(size_t node) const override { return replay.expressions.at(node); }
	Range range(size_t node) const override { return nodes.at(node).range; }
	size_t argumentCount(size_t node) const override { return nodes.at(node).arguments.size(); }
	size_t argument(size_t node, size_t index) const override { return nodes.at(node).arguments.at(index); }
	std::optional<std::string> numericSpelling(size_t node) const override {
		return nodes.at(node).numeric ? std::optional<std::string>(nodes.at(node).range.subString) : std::nullopt;
	}

  private:
	struct Node {
		Range range;
		bool numeric;
		std::vector<size_t> arguments;
	};
	ParseContext &context;
	Section *owner;
	Range root;
	std::string payload;
	std::string_view syntax;
	std::shared_ptr<SourceSyntaxWork> work;
	std::vector<Node> nodes;
	ParsedExpressionReplay replay;
	bool materialized = false, materializing = false;
};
