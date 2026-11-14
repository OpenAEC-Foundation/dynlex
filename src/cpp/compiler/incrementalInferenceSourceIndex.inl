// Canonical source nodes are indexed at the resolved-pattern boundary, before
// inference borrows or groups expressions. Every fact snapshot shares this view.
class InferenceSourceIndex {
  public:
	InferenceSourceIndex(
		ParseContext &context, const std::map<const Section *, std::string> &paths,
		const std::map<std::string, Section *> &sections, BuildStatistics &statistics
	)
		: context(context), paths(paths), sections(sections), statistics(statistics) {
		auto start = Clock::now();
		for (const auto &[_, section] : sections)
			if (std::ranges::any_of(section->codeLines, [](const CodeLine *line) {
				return line->expression.materialized() != nullptr;
			}))
				indexScope(*section);
		requireCompilerInvariant(!context.sourceExpressionPublished, "source expression index already has a publisher");
		context.sourceExpressionPublished = [this](CodeLine *line, Expression *expression) {
			auto started = Clock::now();
			size_t prior = nodes.size();
			if (indexScope(*line->section)) {
				requireCompilerInvariant(
					roots.at(lineRoots.at(line)).expression == expression, "new source scope lost its published root"
				);
			} else {
				size_t root = lineRoots.at(line);
				requireCompilerInvariant(!roots.at(root).expression, "canonical source root was published twice");
				roots.at(root).expression = expression;
				indexExpression(expression, root, nullptr, {});
			}
			this->statistics.inferenceSourceExpressionsIndexed += nodes.size() - prior;
			this->statistics.inferenceSourceIndexMilliseconds += elapsedMilliseconds(started);
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
		if (!definitionsReady) {
			auto started = Clock::now();
			for (const auto &[path, scope] : sections)
				for (const auto &[name, definition] : scope->variableDefinitions)
					definitions.emplace(normalizeBindingReference(definition), Json::array({path, name}));
			definitionsReady = true;
			statistics.inferenceSourceDefinitionsIndexed += definitions.size();
			statistics.inferenceSourceIndexMilliseconds += elapsedMilliseconds(started);
		}
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
		const std::string &path = site.at(0).get_ref<const std::string &>();
		if (!indexedScopes.contains(sections.at(path))) {
			auto started = Clock::now();
			size_t prior = nodes.size();
			indexScope(*sections.at(path));
			statistics.inferenceSourceExpressionsIndexed += nodes.size() - prior;
			statistics.inferenceSourceIndexMilliseconds += elapsedMilliseconds(started);
		}
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
	bool indexScope(const Section &section) const {
		if (!indexedScopes.insert(&section).second)
			return false;
		statistics.inferenceSourceScopesIndexed++;
		const std::string &path = paths.at(&section);
		std::unordered_map<std::string, size_t> occurrences;
		for (CodeLine *line : section.codeLines)
			if (line->expression) {
				std::string text = digest(line->rightTrimmedText);
				size_t root = roots.size();
				roots.push_back({path, text, occurrences[text]++, line, line->expression.materialized()});
				const Root &source = roots.back();
				rootExpressions.emplace(RootKey{source.path, source.text, source.occurrence}, root);
				requireCompilerInvariant(lineRoots.emplace(line, root).second, "canonical source line has two owning scopes");
				indexExpression(source.expression, root, nullptr, {});
			}
		return true;
	}
	void indexExpression(
		const Expression *expression, size_t root, const Expression *parent, std::optional<size_t> component
	) const {
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
	const std::map<const Section *, std::string> &paths;
	const std::map<std::string, Section *> &sections;
	BuildStatistics &statistics;
	// Published views and decoded roots must survive later scope registration.
	mutable std::deque<Root> roots;
	using RootKey = std::tuple<std::string_view, std::string_view, size_t>;
	mutable std::map<RootKey, size_t> rootExpressions;
	mutable std::unordered_map<CodeLine *, size_t> lineRoots;
	mutable std::vector<Expression *> arguments;
	mutable std::unordered_map<const Expression *, Node> nodes;
	mutable std::unordered_set<const Section *> indexedScopes;
	mutable std::unordered_map<const VariableReference *, Json> definitions;
	mutable bool definitionsReady = false;
	mutable std::unordered_map<const Expression *, Json> expressions;
};
