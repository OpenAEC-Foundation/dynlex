static void recordRecursiveClassStorageFailure(
	InferenceContext *inferenceContext, ParseContext &parseContext, Range range, const DataType &type
);

// A symbolic field can be completed by the enclosing class transaction. It is
// not an unspecified generic field: its declaration and pending layout are known.
static bool classFieldTypeIsCompleteOrPending(const DataType &type) {
	if (type.isConcrete())
		return true;
	if (type.kind == DataType::Kind::Class) {
		return std::ranges::any_of(activeClassInstantiations, [&](const ActiveClassInstantiation &active) {
			return type.classDefinition == active.classDefinition && type.classInstIndex == active.symbolicIndex;
		});
	}
	if (type.hasFunctionPayload()) {
		return type.functionSignature && classFieldTypeIsCompleteOrPending(type.functionSignature->result) &&
			   std::ranges::all_of(type.functionSignature->parameters, classFieldTypeIsCompleteOrPending);
	}
	bool validExtent = (type.kind == DataType::Kind::Array && type.arraySize >= 0) ||
					   (type.kind == DataType::Kind::Vector && type.arraySize > 0) ||
					   (type.kind == DataType::Kind::Matrix && type.arraySize > 0 && type.matrixRowCount > 0);
	return validExtent && type.arrayElementType && classFieldTypeIsCompleteOrPending(*type.arrayElementType);
}

enum class ClassStorageVisit { Visiting, Complete };
using ClassStorageKey = std::pair<ClassDefinition *, int>;

// Size is finite exactly when the completed graph of inline fields is acyclic.
// Pointer and function fields do not contain their referenced objects inline.
static bool validateInlineClassStorage(
	const DataType &type, Range range, std::map<ClassStorageKey, ClassStorageVisit> &visits, Range &cycleRange,
	DataType &cycleType
) {
	if (type.isPointer() || type.hasFunctionPayload())
		return true;
	if (type.hasAggregateElementType())
		return validateInlineClassStorage(*type.arrayElementType, range, visits, cycleRange, cycleType);
	if (type.kind != DataType::Kind::Class)
		return true;
	requireCompilerInvariant(
		type.classDefinition && type.classInstIndex >= 0 &&
			type.classInstIndex < static_cast<int>(type.classDefinition->instantiations.size()),
		"completed class storage refers to an unfinished layout"
	);
	auto [visit, inserted] =
		visits.emplace(ClassStorageKey{type.classDefinition, type.classInstIndex}, ClassStorageVisit::Visiting);
	if (!inserted) {
		if (visit->second == ClassStorageVisit::Complete)
			return true;
		cycleRange = range;
		cycleType = type;
		return false;
	}
	const ClassDefinition &definition = *type.classDefinition;
	const auto &fields = definition.instantiations[type.classInstIndex].fieldTypes;
	for (size_t index = 0; index < fields.size(); ++index) {
		if (!validateInlineClassStorage(fields[index], definition.fields[index].range, visits, cycleRange, cycleType))
			return false;
	}
	visit->second = ClassStorageVisit::Complete;
	return true;
}

static bool validateCompletedClassStorage(ParseContext &parseContext, InferenceContext *inferenceContext) {
	requireCompilerInvariant(activeClassInstantiationTransaction.has_value(), "class storage resolution has no transaction");
	std::map<ClassStorageKey, ClassStorageVisit> visits;
	for (ClassDefinition *definition : activeClassInstantiationTransaction->appendedDefinitions) {
		size_t first = activeClassInstantiationTransaction->originalInstantiationSizes.at(definition);
		for (size_t index = first; index < definition->instantiations.size(); ++index) {
			Range cycleRange;
			DataType cycleType;
			if (validateInlineClassStorage(
					{DataType::Kind::Class, 0, 0, definition, static_cast<int>(index)}, definition->range, visits, cycleRange,
					cycleType
				))
				continue;
			recordRecursiveClassStorageFailure(inferenceContext, parseContext, cycleRange, cycleType);
			return false;
		}
	}
	return true;
}
