static void rollbackTrialJournal(InferenceContext &context, InferenceContext::TrialJournal &journal) {
	ParseContext &parseContext = context.parseContext;
	if (parseContext.inferenceProducts && !journal.sectionInstantiationUndo.empty())
		parseContext.inferenceProducts->discardedBatch([&]() {
			for (const auto &undo : journal.sectionInstantiationUndo) {
				auto instance = undo.section->instantiations.find(undo.key);
				requireCompilerInvariant(
					instance != undo.section->instantiations.end(), "discarded query lost its instantiation"
				);
				const Instantiation &current = instance->second;
				const Instantiation &before = undo.value;
				bool bodyRetained = undo.existed && before.valid && !before.inferring && !before.needsReinfer &&
									current.valid && !current.inferring && !current.needsReinfer &&
									before.storageScope == current.storageScope && before.body == current.body;
				// Completed facts already present before this trial survive its undo.
				// Only new pure observations need a separate product in that case.
				if ((bodyRetained && before.pureReturnValuesByArguments.size() == current.pureReturnValuesByArguments.size() &&
					 before.pureReturnValuesComplete == current.pureReturnValuesComplete) ||
					(undo.existed && before.failure && before.failure == current.failure))
					continue;
				parseContext.inferenceProducts->discarded(
					undo.section, undo.key, current, bodyRetained, [&](const Expression *expression) {
					return context.lookupExpressionValue(const_cast<Expression *>(expression));
				}
				);
			}
		});

	for (auto it = journal.borrowedExpressionUndo.rbegin(); it != journal.borrowedExpressionUndo.rend(); ++it)
		*it->expression = std::move(it->value);
	for (Section *section : journal.touchedSections)
		resetSectionExpressionTypes(section);
	for (auto it = journal.variableTypeUndo.rbegin(); it != journal.variableTypeUndo.rend(); ++it) {
		it->variable->type = it->type;
		it->variable->typeOriginRange = it->typeOriginRange;
		it->variable->typeOriginFloatLiteralReplacement = it->typeOriginFloatLiteralReplacement;
	}
	for (auto it = journal.instantiationUndo.rbegin(); it != journal.instantiationUndo.rend(); ++it) {
		*it->instantiation = std::move(*it->value);
	}
	for (auto it = journal.sectionInstantiationUndo.rbegin(); it != journal.sectionInstantiationUndo.rend(); ++it) {
		auto instantiationIt = it->section->instantiations.find(it->key);
		if (it->existed) {
			if (instantiationIt == it->section->instantiations.end()) {
				crashCompilerBug(
					"trial rollback lost a previously-existing section instantiation; instantiation map changed unexpectedly"
				);
			}
			instantiationIt->second = it->value;
			continue;
		}
		if (instantiationIt == it->section->instantiations.end())
			crashCompilerBug("trial rollback expected to erase a provisional section instantiation, but it was missing");
		it->section->instantiations.erase(instantiationIt);
	}
	for (auto it = journal.classRequestInsertions.rbegin(); it != journal.classRequestInsertions.rend(); ++it)
		it->first->instantiationRequests.erase(it->second);
	for (auto it = journal.classInstantiationSizes.rbegin(); it != journal.classInstantiationSizes.rend(); ++it) {
		if (it->first->instantiations.size() < it->second)
			crashCompilerBug("trial rollback observed class instantiation list shrink below recorded snapshot");
		it->first->instantiations.resize(it->second);
	}
}
