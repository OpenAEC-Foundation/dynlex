static std::unordered_set<PatternFamily *> buildPatternFamilies(ParseContext &context) {
	requireCompilerInvariant(context.patternFamilies.empty(), "pattern families must be built exactly once");
	std::unordered_set<PatternTreeNode *> visitedEndpoints;
	std::unordered_set<PatternFamily *> generatedAccessorFamilies;
	std::function<void(Section *)> collect = [&](Section *section) {
		for (PatternDefinition *definition : section->patternDefinitions) {
			if (!definition->family) {
				auto *family = context.patternFamilies.emplace_back(std::make_unique<PatternFamily>()).get();
				definition->family = family;
				std::stack<PatternDefinition *> pending;
				pending.push(definition);
				while (!pending.empty()) {
					PatternDefinition *current = pending.top();
					pending.pop();
					for (PatternTreeNode *endpoint : current->endNodes) {
						// Scan each endpoint's overload list once, regardless of family size.
						if (!visitedEndpoints.insert(endpoint).second)
							continue;
						for (PatternDefinition *member : endpoint->matchingDefinitions) {
							if (!member->family) {
								member->family = family;
								pending.push(member);
							} else {
								requireCompilerInvariant(member->family == family, "shared endpoint spans syntax families");
							}
						}
					}
				}
			}
			if (definition->isGeneratedClassPropertyAccessor)
				generatedAccessorFamilies.insert(definition->family);
		}
		for (Section *child : section->children)
			collect(child);
	};
	collect(context.mainSection);
	return generatedAccessorFamilies;
}

static bool resolvePatternPrecedence(ParseContext &context) {
	const auto generatedAccessorFamilies = buildPatternFamilies(context);
	struct FamilyEdges {
		std::unordered_set<PatternFamily *> successors;
		size_t incoming = 0;
	};
	std::unordered_map<PatternFamily *, FamilyEdges> graph;
	auto addEdge = [&](PatternFamily *higher, PatternFamily *lower) {
		graph.try_emplace(lower);
		if (graph[higher].successors.insert(lower).second)
			graph.at(lower).incoming++;
	};
	// "default" connects declarations before and after it without becoming a
	// persistent syntax family or imposing an order on otherwise unrelated peers.
	PatternFamily defaultSentinel;
	auto addDeclaration = [&](PatternDefinition *definition, const std::string &signature, bool before) {
		auto connect = [&](PatternFamily *target) {
			if (before)
				addEdge(definition->family, target);
			else
				addEdge(target, definition->family);
		};
		if (signature == "default") {
			connect(&defaultSentinel);
			return true;
		}
		const lsp::SourceFile *sourceFile = definition->range.line ? definition->range.line->sourceFile : nullptr;
		auto targets = findDefinitionsBySignature(context, SectionType::Function, signature, sourceFile);
		if (targets.empty()) {
			context.diagnostics.push_back(Diagnostic(
				context, Diagnostic::Level::Error, "precedence target not found", definition->range, "target", signature
			));
			return false;
		}
		for (PatternDefinition *target : targets)
			connect(target->family);
		return true;
	};
	std::function<bool(Section *)> collect = [&](Section *section) {
		for (PatternDefinition *definition : section->patternDefinitions) {
			for (const std::string &signature : section->beforePatterns)
				if (!addDeclaration(definition, signature, true))
					return false;
			for (const std::string &signature : section->afterPatterns)
				if (!addDeclaration(definition, signature, false))
					return false;
		}
		for (Section *child : section->children)
			if (!collect(child))
				return false;
		return true;
	};
	if (!collect(context.mainSection))
		return false;

	std::vector<PatternFamily *> precedenceTargets;
	for (const auto &[family, edges] : graph)
		precedenceTargets.push_back(family);
	for (PatternFamily *family : generatedAccessorFamilies) {
		// All syntax families containing generated accessors bind before the
		// authored graph, without creating automatic edges between those families.
		for (PatternFamily *target : precedenceTargets)
			if (!generatedAccessorFamilies.contains(target))
				addEdge(family, target);
	}

	std::queue<PatternFamily *> ready;
	for (const auto &[family, edges] : graph)
		if (edges.incoming == 0)
			ready.push(family);
	std::vector<PatternFamily *> order;
	while (!ready.empty()) {
		PatternFamily *family = ready.front();
		ready.pop();
		order.push_back(family);
		for (PatternFamily *successor : graph.at(family).successors)
			if (--graph.at(successor).incoming == 0)
				ready.push(successor);
	}
	if (order.size() != graph.size()) {
		context.diagnostics.push_back(Diagnostic(context, Diagnostic::Level::Error, "precedence cycle detected", Range()));
		return false;
	}

	// Successor closures are ready before their predecessors. Store reachability
	// once per family, never as the Cartesian product of individual overloads.
	for (PatternFamily *family : std::views::reverse(order)) {
		for (PatternFamily *successor : graph.at(family).successors) {
			if (successor != &defaultSentinel)
				family->precedenceSuccessors.insert(successor);
			family->precedenceSuccessors.insert(successor->precedenceSuccessors.begin(), successor->precedenceSuccessors.end());
		}
	}
	return true;
}
