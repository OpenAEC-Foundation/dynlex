#pragma once

#include <queue>

static bool typeHasRefinableStructure(const DataType &type, std::set<std::pair<const ClassDefinition *, int>> &visited) {
	if (!type.isDeduced())
		return true;
	if (type.kind == DataType::Kind::Class) {
		requireCompilerInvariant(type.classDefinition, "refinable class type has no definition");
		if (type.classInstIndex < 0)
			return true;
		if (!visited.insert({type.classDefinition, type.classInstIndex}).second)
			return false;
		for (const DataType &field : type.classDefinition->instantiations.at(type.classInstIndex).fieldTypes)
			if (typeHasRefinableStructure(field, visited))
				return true;
	}
	return type.arrayElementType && typeHasRefinableStructure(*type.arrayElementType, visited);
}

static bool typeHasRefinableStructure(const DataType &type) {
	std::set<std::pair<const ClassDefinition *, int>> visited;
	return typeHasRefinableStructure(type, visited);
}

static bool mergeStorageType(const DataType &current, const DataType &incoming, DataType &merged) {
	if (!current.isDeduced()) {
		merged = incoming;
		return true;
	}
	return mergeVariableAssignmentType(current, incoming, merged);
}

static void publishRefinedRootType(const VariableStorageKey &root, const DataType &type, InferenceContext &context) {
	// A retained equation can refer to a completed macro invocation. Its type
	// remains available, but the declaration's reusable Variable belongs to the
	// currently active invocation only.
	if (variableStorageKey(root.definition, context.currentInstantiatedSectionBody) != root)
		return;
	Variable *variable = context.typeRefinements.read().roots.at(root).variable;
	if (variable->type != type) {
		if (context.trial)
			context.trialJournal->recordVariableWrite(variable);
		variable->type = type;
	}
	recordParameterOutputType(context, variable);
}

static DataType projectStorageType(const DataType &root, const std::vector<StorageTypeProjection> &projections) {
	DataType type = root;
	for (const auto &projection : projections) {
		if (projection.kind == StorageTypeProjection::Kind::PointerDepth) {
			type.pointerDepth += projection.index;
			requireCompilerInvariant(type.pointerDepth >= 0, "storage type view dereferences a non-pointer");
		} else if (projection.kind == StorageTypeProjection::Kind::Element) {
			type = type.aggregateElementType();
		} else {
			requireCompilerInvariant(
				type.kind == DataType::Kind::Class && type.classDefinition && type.classInstIndex >= 0,
				"storage type view lost its class owner"
			);
			type = type.classDefinition->instantiations[type.classInstIndex].fieldTypes.at(projection.index);
		}
	}
	return type;
}

static bool refineProjectedStorageType(
	DataType &type, const std::vector<StorageTypeProjection> &projections, size_t index, const DataType &candidate,
	InferenceContext &context
) {
	if (index == projections.size()) {
		DataType merged;
		if (!mergeStorageType(type, candidate, merged))
			return false;
		type = std::move(merged);
		return true;
	}
	const auto &projection = projections[index];
	if (projection.kind == StorageTypeProjection::Kind::PointerDepth) {
		DataType projected = type;
		projected.pointerDepth += projection.index;
		if (!refineProjectedStorageType(projected, projections, index + 1, candidate, context))
			return false;
		projected.pointerDepth -= projection.index;
		type = std::move(projected);
		return true;
	}
	if (projection.kind == StorageTypeProjection::Kind::Element) {
		DataType element = type.aggregateElementType();
		if (!refineProjectedStorageType(element, projections, index + 1, candidate, context))
			return false;
		type.arrayElementType = std::make_shared<DataType>(std::move(element));
		return true;
	}
	DataType field = type.classDefinition->instantiations[type.classInstIndex].fieldTypes.at(projection.index);
	if (!refineProjectedStorageType(field, projections, index + 1, candidate, context))
		return false;
	int refined =
		getRefinedClassInstantiationIndex(context, type.classDefinition, type.classInstIndex, projection.index, field);
	if (refined < 0)
		return false;
	type.classInstIndex = refined;
	return true;
}

static bool propagateStorageTypes(InferenceContext &context, const std::vector<VariableStorageKey> &changed) {
	auto &storage = context.typeRefinements.write();
	std::queue<VariableStorageKey> pending;
	std::set<VariableStorageKey> queued;
	for (const auto &root : changed)
		if (queued.insert(root).second)
			pending.push(root);
	while (!pending.empty()) {
		VariableStorageKey root = pending.front();
		pending.pop();
		queued.erase(root);
		publishRefinedRootType(root, storage.roots.at(root).type, context);
		for (auto relation = storage.equalViews.lower_bound({root, {}});
			 relation != storage.equalViews.end() && relation->first.root == root; ++relation) {
			const auto &view = relation->first;
			for (const auto &other : relation->second) {
				DataType left = projectStorageType(storage.roots.at(root).type, view.projections);
				DataType right = projectStorageType(storage.roots.at(other.root).type, other.projections);
				DataType merged;
				if (!mergeStorageType(left, right, merged)) {
					context.setTypeFailure("Incompatible type refinements for shared storage '" + root.definition->name + "'");
					return false;
				}
				for (const auto *target : {&view, &other}) {
					DataType &rootType = storage.roots.at(target->root).type;
					DataType previous = rootType;
					if (!refineProjectedStorageType(rootType, target->projections, 0, merged, context)) {
						context.setTypeFailure(
							"Incompatible type refinements for shared storage '" + target->root.definition->name + "'"
						);
						return false;
					}
					if (rootType != previous && queued.insert(target->root).second)
						pending.push(target->root);
				}
			}
		}
	}
	return true;
}

static bool
refineStorageRootType(const VariableStorageKey &root, Variable *variable, const DataType &type, InferenceContext &context) {
	const auto &roots = context.typeRefinements.read().roots;
	auto position = roots.find(root);
	DataType merged;
	if (!mergeStorageType(position == roots.end() ? DataType{} : position->second.type, type, merged)) {
		context.setTypeFailure("Incompatible type refinements for shared storage '" + root.definition->name + "'");
		return false;
	}
	if (position != roots.end() && position->second.type == merged) {
		publishRefinedRootType(root, merged, context);
		return true;
	}
	context.typeRefinements.write().roots[root] = {variable, std::move(merged)};
	return propagateStorageTypes(context, {root});
}

static std::optional<StorageTypeView> storageTypeView(
	Expression *expression, InferenceContext &context, const BindingFrameStack &bindings, bool requireRoot = false
) {
	ResolvedBindingLayers resolved = resolveInferenceBindingLayers(expression, bindings, &context);
	Expression *value = resolved.expression;
	requireCompilerInvariant(value != nullptr, "storage type view lost its expression");
	if (value->kind == Expression::Kind::Variable && value->variable) {
		VariableStorageKey root = variableStorageKey(value);
		Variable *variable = value->range.line->section->findVariable(value->variable);
		requireCompilerInvariant(variable != nullptr, "storage type view lost its variable");
		// Closed types cannot acquire further structure. Existing roots still
		// receive writes so an earlier equation can complete its other endpoint.
		if (!requireRoot && !context.typeRefinements.read().roots.contains(root) && !typeHasRefinableStructure(variable->type))
			return std::nullopt;
		if (!refineStorageRootType(root, variable, variable->type, context))
			return std::nullopt;
		return StorageTypeView{root, {}};
	}
	if (value->inferredPointerStorage)
		return storageTypeView(value->inferredPointerStorage, context, resolved.bindingFrameStack, requireRoot);
	if (value->kind != Expression::Kind::IntrinsicCall)
		return std::nullopt;
	IntrinsicKind kind = intrinsicKind(value->intrinsicName);
	if (kind == IntrinsicKind::Subject) {
		auto source = context.typeRefinements.read().subjectSources.find(value->subjectSetter);
		if (source == context.typeRefinements.read().subjectSources.end() && !typeHasRefinableStructure(value->type))
			return std::nullopt;
		requireCompilerInvariant(
			source != context.typeRefinements.read().subjectSources.end(), "subject type view lost its captured source"
		);
		ExpressionCapture capture = source->second;
		return storageTypeView(capture.data->expression(), context, capture.data->bindings, requireRoot);
	}
	if (kind != IntrinsicKind::Property && kind != IntrinsicKind::AddressOf && kind != IntrinsicKind::Dereference &&
		kind != IntrinsicKind::Cast && kind != IntrinsicKind::ExtractElement)
		return std::nullopt;
	if (kind == IntrinsicKind::Cast) {
		if (value->arguments[1]->type != value->type)
			return std::nullopt; // An explicit representation-changing cast is not a type equation.
	}
	auto view = storageTypeView(value->arguments[1], context, resolved.bindingFrameStack, requireRoot);
	if (!view)
		return std::nullopt;
	if (kind == IntrinsicKind::Property) {
		DataType owner = value->arguments[1]->type;
		CompileTimeValue field = resolveStoredCompileTimeValue(value->arguments[2], resolved.bindingFrameStack, &context);
		const auto *name = std::get_if<std::string>(&field);
		requireCompilerInvariant(name != nullptr && owner.classDefinition != nullptr, "property type view lost its field");
		auto position = std::ranges::find(owner.classDefinition->fields, *name, &FieldDefinition::name);
		requireCompilerInvariant(position != owner.classDefinition->fields.end(), "property type view has no declared field");
		view->projections.push_back(
			{StorageTypeProjection::Kind::Field, static_cast<int>(position - owner.classDefinition->fields.begin())}
		);
	} else if (kind == IntrinsicKind::ExtractElement) {
		view->projections.push_back({StorageTypeProjection::Kind::Element, 0});
	} else if (kind != IntrinsicKind::Cast) {
		view->projections.push_back({StorageTypeProjection::Kind::PointerDepth, kind == IntrinsicKind::AddressOf ? 1 : -1});
	}
	return view;
}

static bool recordStorageTypeEquality(const StorageTypeView &left, const StorageTypeView &right, InferenceContext &context) {
	if (left == right)
		return true;
	const auto &existing = context.typeRefinements.read().equalViews;
	auto previous = existing.find(left);
	if (previous != existing.end() && previous->second.contains(right))
		return true;
	auto &equations = context.typeRefinements.write().equalViews;
	equations[left].insert(right);
	equations[right].insert(left);
	return propagateStorageTypes(context, {left.root, right.root});
}

static void recordStorageTypeAssignment(
	Expression *destination, const BindingFrameStack &destinationBindings, Expression *source,
	const BindingFrameStack &sourceBindings, InferenceContext &context
) {
	bool requireRoot =
		typeHasRefinableStructure(destination->type) || typeHasRefinableStructure(effectiveInferredExpressionType(source));
	auto left = storageTypeView(destination, context, destinationBindings, requireRoot);
	if (!left || !context.typesValid)
		return;
	auto right = storageTypeView(source, context, sourceBindings, requireRoot);
	if (right && !recordStorageTypeEquality(*left, *right, context))
		return;
	ResolvedBindingLayers resolved = resolveInferenceBindingLayers(source, sourceBindings, &context);
	Expression *value = resolved.expression;
	if (value->kind == Expression::Kind::ArrayLiteral) {
		StorageTypeView element = *left;
		element.projections.push_back({StorageTypeProjection::Kind::Element, 0});
		for (Expression *argument : value->arguments) {
			auto sourceElement = storageTypeView(argument, context, resolved.bindingFrameStack);
			if (sourceElement && !recordStorageTypeEquality(element, *sourceElement, context))
				return;
		}
		return;
	}
	if (value->kind != Expression::Kind::IntrinsicCall || intrinsicKind(value->intrinsicName) != IntrinsicKind::Construct ||
		value->type.kind != DataType::Kind::Class || value->type.isPointer())
		return;
	for (size_t index = 2; index < value->arguments.size(); ++index) {
		if (!typeHasRefinableStructure(value->arguments[index]->type))
			continue;
		auto fieldSource = storageTypeView(value->arguments[index], context, resolved.bindingFrameStack);
		if (!fieldSource)
			continue;
		StorageTypeView field = *left;
		field.projections.push_back({StorageTypeProjection::Kind::Field, static_cast<int>(index - 2)});
		if (!recordStorageTypeEquality(field, *fieldSource, context))
			return;
	}
}

static void restoreRefinedVariableType(Expression *expression, Variable *variable, InferenceContext &context) {
	auto found = context.typeRefinements.read().roots.find(variableStorageKey(expression));
	if (found != context.typeRefinements.read().roots.end() && variable->type != found->second.type)
		publishRefinedRootType(found->first, found->second.type, context);
}

static void recordSubjectTypeSource(
	Expression *setter, const DataType &type, InferenceContext &context, const BindingFrameStack &bindings
) {
	if (typeHasRefinableStructure(type))
		context.typeRefinements.write().subjectSources[setter] = captureExpression(context.parseContext, setter, 1, bindings);
}
