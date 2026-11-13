// Canonical source nodes are indexed at the resolved-pattern boundary, before
// inference borrows or groups expressions. Every fact snapshot shares this view.
class InferenceSourceIndex {
  public:
	InferenceSourceIndex(
		ParseContext &context, const std::map<const Section *, std::string> &paths, BuildStatistics &statistics
	)
		: context(context), statistics(statistics) {
		auto start = Clock::now();
		std::map<std::string, const Section *> sections;
		for (const auto &[section, path] : paths)
			sections.emplace(path, section);
		for (const auto &[path, section] : sections) {
			for (const auto &[name, definition] : section->variableDefinitions)
				definitions.emplace(normalizeBindingReference(definition), Json::array({path, name}));
			std::unordered_map<std::string, size_t> occurrences;
			for (CodeLine *line : section->codeLines)
				if (line->expression) {
					std::string text = digest(line->rightTrimmedText);
					size_t root = roots.size();
					roots.push_back({path, text, occurrences[text]++, line, line->expression.materialized()});
					lineRoots.emplace(line, root);
					indexExpression(line->expression.materialized(), root, nullptr, {});
				}
		}
		for (size_t index = 0; index < roots.size(); index++) {
			const Root &root = roots.at(index);
			rootExpressions.emplace(RootKey{root.path, root.text, root.occurrence}, index);
		}
		requireCompilerInvariant(!context.sourceExpressionPublished, "source expression index already has a publisher");
		context.sourceExpressionPublished = [&](CodeLine *line, Expression *expression) {
			auto started = Clock::now();
			size_t prior = nodes.size();
			size_t root = lineRoots.at(line);
			requireCompilerInvariant(!roots.at(root).expression, "canonical source root was published twice");
			roots.at(root).expression = expression;
			indexExpression(expression, root, nullptr, {});
			statistics.inferenceSourceExpressionsIndexed += nodes.size() - prior;
			statistics.inferenceSourceIndexMilliseconds += elapsedMilliseconds(started);
		};
		statistics.inferenceSourceIndexBuilds++;
		statistics.inferenceSourceExpressionsIndexed += nodes.size();
		statistics.inferenceSourceIndexMilliseconds += elapsedMilliseconds(start);
	}
	~InferenceSourceIndex() { context.sourceExpressionPublished = {}; }
	void recordStorageIndex(size_t count) const {
		statistics.inferenceStorageIndicesBuilt++;
		statistics.inferenceStorageOriginsIndexed += count;
	}
	const Json &definition(const VariableReference *reference) const {
		auto found = definitions.find(reference);
		requireCompilerInvariant(found != definitions.end(), "storage origin has no lexical declaration");
		return found->second;
	}
	std::optional<Json> expression(const Expression *expression) const {
		auto found = nodes.find(expression);
		if (found == nodes.end())
			return {};
		auto [encoded, inserted] = expressions.try_emplace(expression);
		if (inserted) {
			const auto &root = roots.at(found->second.root);
			std::vector<Json> components;
			for (const Node *node = &found->second; node->parent; node = &nodes.at(node->parent))
				components.push_back(node->component ? Json(*node->component) : Json(nullptr));
			std::ranges::reverse(components);
			encoded->second = Json::array({root.path, root.text, root.occurrence, std::move(components)});
			statistics.inferenceSourceExpressionsEncoded++;
		}
		return encoded->second;
	}
	Expression *decodeExpression(const Json &site) const {
		statistics.inferenceSourceExpressionLookups++;
		auto root = rootExpressions.find(
			RootKey{
				site.at(0).get_ref<const std::string &>(), site.at(1).get_ref<const std::string &>(), site.at(2).get<size_t>()
			}
		);
		requireCompilerInvariant(root != rootExpressions.end(), "cached storage invocation lost its source expression");
		const Root &source = roots.at(root->second);
		if (!source.expression)
			(void)static_cast<Expression *>(source.line->expression);
		requireCompilerInvariant(source.expression, "source syntax demand did not publish its canonical root");
		Expression *expression = source.expression;
		for (const Json &component : site.at(3)) {
			const Node &node = nodes.at(expression);
			if (component.is_null())
				expression = node.declaration;
			else {
				size_t index = component.get<size_t>();
				requireCompilerInvariant(index < node.argumentCount, "cached source path lost an argument");
				expression = arguments.at(node.argumentStart + index);
			}
			requireCompilerInvariant(expression, "cached source path lost its expression");
		}
		return expression;
	}

  private:
	struct Root {
		std::string path, text;
		size_t occurrence;
		CodeLine *line;
		Expression *expression;
	};
	struct Node {
		size_t root;
		const Expression *parent;
		std::optional<size_t> component;
		size_t argumentStart, argumentCount;
		Expression *declaration;
	};
	void indexExpression(const Expression *expression, size_t root, const Expression *parent, std::optional<size_t> component) {
		if (!expression ||
			!nodes
				 .emplace(
					 expression,
					 Node{root, parent, component, arguments.size(), expression->arguments.size(), expression->dataDeclaration}
				 )
				 .second)
			return;
		arguments.insert(arguments.end(), expression->arguments.begin(), expression->arguments.end());
		for (size_t argument = 0; argument < expression->arguments.size(); argument++)
			indexExpression(expression->arguments[argument], root, expression, argument);
		indexExpression(expression->dataDeclaration, root, expression, {});
	}
	ParseContext &context;
	BuildStatistics &statistics;
	std::vector<Root> roots;
	using RootKey = std::tuple<std::string_view, std::string_view, size_t>;
	std::map<RootKey, size_t> rootExpressions;
	std::unordered_map<CodeLine *, size_t> lineRoots;
	std::vector<Expression *> arguments;
	std::unordered_map<const Expression *, Node> nodes;
	std::unordered_map<const VariableReference *, Json> definitions;
	mutable std::unordered_map<const Expression *, Json> expressions;
};
