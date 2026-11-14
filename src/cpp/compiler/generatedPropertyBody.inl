// The producer publishes its binding before matching. The executable body is
// already a resolved intrinsic and can wait until a semantic consumer needs it.
class GeneratedPropertyBody : public BodySyntaxProduct {
  public:
	GeneratedPropertyBody(
		ParseContext &context, FunctionSection &owner, CodeLine &line, VariableReference &receiver, std::string field
	)
		: context(context), owner(owner), line(line), receiver(receiver), field(std::move(field)) {}

	void demand() override {
		if (materialized)
			return;
		Range range(&line, line.fullText);
		auto *intrinsic = new Expression();
		intrinsic->kind = Expression::Kind::IntrinsicCall;
		intrinsic->intrinsicName = "property";
		intrinsic->range = range;

		auto *name = new Expression();
		name->kind = Expression::Kind::Literal;
		name->literalValue = std::string("property");
		name->range = range;
		intrinsic->arguments.push_back(name);

		auto *instance = new Expression();
		instance->kind = Expression::Kind::Variable;
		instance->range = range;
		instance->variable = &receiver;
		intrinsic->arguments.push_back(instance);

		auto *property = new Expression();
		property->kind = Expression::Kind::Literal;
		property->literalValue = field;
		property->range = range;
		intrinsic->arguments.push_back(property);

		line.expression.publish(*intrinsic);
		materialized = true;
		line.expression.finish();
		if (context.sourceExpressionPublished)
			context.sourceExpressionPublished(&line, intrinsic);
	}
	bool syntaxMaterialized() const override { return materialized; }
	PatternReference *rootReference() const override { return nullptr; }
	const Section &bodyOwner() const override { return owner; }

  private:
	ParseContext &context;
	FunctionSection &owner;
	CodeLine &line;
	VariableReference &receiver;
	std::string field;
	bool materialized = false;
};
