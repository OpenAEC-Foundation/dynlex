#include "dependentTypeConstraint.h"
#include "compileTimeValue.h"
#include <algorithm>
#include <limits>

namespace {
void appendDependency(std::vector<ConstraintDependency> &dependencies, ConstraintDependency dependency) {
	if (std::ranges::find(dependencies, dependency) == dependencies.end())
		dependencies.push_back(dependency);
}

std::optional<std::int64_t> checkedBinary(ConstraintIntegerTerm::Kind kind, std::int64_t left, std::int64_t right) {
	switch (kind) {
	case ConstraintIntegerTerm::Kind::Add:
		if ((right > 0 && left > std::numeric_limits<std::int64_t>::max() - right) ||
			(right < 0 && left < std::numeric_limits<std::int64_t>::min() - right))
			return std::nullopt;
		return left + right;
	case ConstraintIntegerTerm::Kind::Subtract:
		if ((right < 0 && left > std::numeric_limits<std::int64_t>::max() + right) ||
			(right > 0 && left < std::numeric_limits<std::int64_t>::min() + right))
			return std::nullopt;
		return left - right;
	case ConstraintIntegerTerm::Kind::Multiply:
		if (left == 0 || right == 0)
			return 0;
		if ((left == -1 && right == std::numeric_limits<std::int64_t>::min()) ||
			(right == -1 && left == std::numeric_limits<std::int64_t>::min()))
			return std::nullopt;
		if (left > 0) {
			if ((right > 0 && left > std::numeric_limits<std::int64_t>::max() / right) ||
				(right < 0 && right < std::numeric_limits<std::int64_t>::min() / left))
				return std::nullopt;
		} else if ((right > 0 && left < std::numeric_limits<std::int64_t>::min() / right) ||
				   (right < 0 && left < std::numeric_limits<std::int64_t>::max() / right)) {
			return std::nullopt;
		}
		return left * right;
	case ConstraintIntegerTerm::Kind::Divide:
		if (right == 0 || (left == std::numeric_limits<std::int64_t>::min() && right == -1))
			return std::nullopt;
		return left / right;
	case ConstraintIntegerTerm::Kind::Modulo:
		if (right == 0 || (left == std::numeric_limits<std::int64_t>::min() && right == -1))
			return std::nullopt;
		return left % right;
	default:
		return std::nullopt;
	}
}

void overlayConstraint(TypeConstraint &destination, const TypeConstraint &source) {
	if (source.kind)
		destination.kind = source.kind;
	if (source.numericSize)
		destination.numericSize = source.numericSize;
	if (source.pointerDepth)
		destination.pointerDepth = source.pointerDepth;
	if (source.arraySize)
		destination.arraySize = source.arraySize;
	if (source.matrixRows)
		destination.matrixRows = source.matrixRows;
	if (source.matrixColumns)
		destination.matrixColumns = source.matrixColumns;
	destination.requiresNumeric = destination.requiresNumeric || source.requiresNumeric;
	if (source.constrainsClassDefinition) {
		destination.constrainsClassDefinition = true;
		destination.classDefinition = source.classDefinition;
	}
	if (source.classInstantiationIndex)
		destination.classInstantiationIndex = source.classInstantiationIndex;
	if (source.elementConstraint)
		destination.elementConstraint = std::make_shared<TypeConstraint>(*source.elementConstraint);
	destination.requiresCompileTimeValue = destination.requiresCompileTimeValue || source.requiresCompileTimeValue;
}

std::optional<TypeConstraint>
projectType(const ConstraintTypeProjection &projection, const std::vector<DataType> &argumentTypes) {
	if (projection.sourceArgumentIndex >= argumentTypes.size())
		return std::nullopt;
	const DataType *type = &argumentTypes[projection.sourceArgumentIndex];
	for (size_t depth = 0; depth < projection.elementDepth; depth++) {
		if (!type->arrayElementType)
			return std::nullopt;
		type = type->arrayElementType.get();
	}
	return TypeConstraint::fromValueType(*type);
}

bool assignMaterializedInteger(
	std::optional<int> &target, const std::optional<ConstraintIntegerTerm> &term, const std::vector<DataType> &argumentTypes,
	const std::vector<CompileTimeValue> &argumentValues
) {
	if (!term)
		return true;
	std::optional<std::int64_t> value = term->materialize(argumentTypes, argumentValues);
	if (!value || *value < std::numeric_limits<int>::min() || *value > std::numeric_limits<int>::max())
		return false;
	target = static_cast<int>(*value);
	return true;
}
} // namespace

bool ConstraintIntegerTerm::operator==(const ConstraintIntegerTerm &other) const {
	if (kind != other.kind || constant != other.constant || sourceArgumentIndex != other.sourceArgumentIndex ||
		dimension != other.dimension || static_cast<bool>(left) != static_cast<bool>(other.left) ||
		static_cast<bool>(right) != static_cast<bool>(other.right))
		return false;
	return (!left || *left == *other.left) && (!right || *right == *other.right);
}

ConstraintIntegerTerm ConstraintIntegerTerm::constantValue(std::int64_t value) {
	ConstraintIntegerTerm result;
	result.constant = value;
	return result;
}

ConstraintIntegerTerm ConstraintIntegerTerm::typeExtent(size_t argumentIndex, int dimension) {
	ConstraintIntegerTerm result;
	result.kind = Kind::TypeExtent;
	result.sourceArgumentIndex = argumentIndex;
	result.dimension = dimension;
	return result;
}

ConstraintIntegerTerm ConstraintIntegerTerm::fixedArgument(size_t argumentIndex) {
	ConstraintIntegerTerm result;
	result.kind = Kind::FixedArgument;
	result.sourceArgumentIndex = argumentIndex;
	return result;
}

ConstraintIntegerTerm
ConstraintIntegerTerm::binary(Kind operation, ConstraintIntegerTerm leftTerm, ConstraintIntegerTerm rightTerm) {
	ConstraintIntegerTerm result;
	result.kind = operation;
	result.left = std::make_shared<ConstraintIntegerTerm>(std::move(leftTerm));
	result.right = std::make_shared<ConstraintIntegerTerm>(std::move(rightTerm));
	return result;
}

std::optional<std::int64_t> ConstraintIntegerTerm::materialize(
	const std::vector<DataType> &argumentTypes, const std::vector<CompileTimeValue> &argumentValues
) const {
	switch (kind) {
	case Kind::Constant:
		return constant;
	case Kind::TypeExtent:
		if (sourceArgumentIndex >= argumentTypes.size())
			return std::nullopt;
		return argumentTypes[sourceArgumentIndex].extent(dimension);
	case Kind::FixedArgument:
		if (sourceArgumentIndex >= argumentValues.size())
			return std::nullopt;
		return getCompileTimeIntegerValue(argumentValues[sourceArgumentIndex]);
	default:
		if (!left || !right)
			return std::nullopt;
		std::optional<std::int64_t> leftValue = left->materialize(argumentTypes, argumentValues);
		std::optional<std::int64_t> rightValue = right->materialize(argumentTypes, argumentValues);
		if (!leftValue || !rightValue)
			return std::nullopt;
		return checkedBinary(kind, *leftValue, *rightValue);
	}
}

void ConstraintIntegerTerm::collectDependencies(std::vector<ConstraintDependency> &out) const {
	if (kind == Kind::TypeExtent)
		appendDependency(out, {sourceArgumentIndex, ConstraintAccess::Type});
	else if (kind == Kind::FixedArgument)
		appendDependency(out, {sourceArgumentIndex, ConstraintAccess::CompileTimeValue});
	if (left)
		left->collectDependencies(out);
	if (right)
		right->collectDependencies(out);
}

TypeConstraintTemplate TypeConstraintTemplate::constant(TypeConstraint constraint) {
	TypeConstraintTemplate result;
	result.constantPart = std::move(constraint);
	return result;
}

bool TypeConstraintTemplate::operator==(const TypeConstraintTemplate &other) const {
	auto equalTerm = [](const auto &left, const auto &right) {
		return left.has_value() == right.has_value() && (!left || *left == *right);
	};
	if (!constantPart.equivalentTo(other.constantPart) || rootProjection != other.rootProjection ||
		!equalTerm(numericSize, other.numericSize) || !equalTerm(pointerDepth, other.pointerDepth) ||
		!equalTerm(arraySize, other.arraySize) || !equalTerm(matrixRows, other.matrixRows) ||
		!equalTerm(matrixColumns, other.matrixColumns) ||
		static_cast<bool>(elementConstraint) != static_cast<bool>(other.elementConstraint) ||
		dependencies != other.dependencies)
		return false;
	return !elementConstraint || *elementConstraint == *other.elementConstraint;
}

TypeConstraintTemplate TypeConstraintTemplate::projectedType(size_t argumentIndex, size_t elementDepth) {
	TypeConstraintTemplate result;
	result.rootProjection = ConstraintTypeProjection{argumentIndex, elementDepth};
	result.collectDependencies();
	return result;
}

bool TypeConstraintTemplate::isDependent() const { return !dependencies.empty(); }

TypeConstraint TypeConstraintTemplate::structuralEnvelope() const {
	TypeConstraint result = constantPart;
	if (elementConstraint)
		result.elementConstraint = std::make_shared<TypeConstraint>(elementConstraint->structuralEnvelope());
	return result;
}

void TypeConstraintTemplate::collectDependencies() {
	dependencies.clear();
	if (rootProjection)
		appendDependency(dependencies, {rootProjection->sourceArgumentIndex, ConstraintAccess::Type});
	for (const auto *term : {&numericSize, &pointerDepth, &arraySize, &matrixRows, &matrixColumns}) {
		if (*term)
			(*term)->collectDependencies(dependencies);
	}
	if (elementConstraint) {
		elementConstraint->collectDependencies();
		for (const ConstraintDependency &dependency : elementConstraint->dependencies)
			appendDependency(dependencies, dependency);
	}
	std::ranges::sort(dependencies);
}

std::optional<TypeConstraint> TypeConstraintTemplate::materialize(
	const std::vector<DataType> &argumentTypes, const std::vector<CompileTimeValue> &argumentValues
) const {
	TypeConstraint result = TypeConstraint::any();
	if (rootProjection) {
		std::optional<TypeConstraint> projected = projectType(*rootProjection, argumentTypes);
		if (!projected)
			return std::nullopt;
		result = std::move(*projected);
	}
	overlayConstraint(result, constantPart);
	if (!assignMaterializedInteger(result.numericSize, numericSize, argumentTypes, argumentValues) ||
		!assignMaterializedInteger(result.pointerDepth, pointerDepth, argumentTypes, argumentValues) ||
		!assignMaterializedInteger(result.arraySize, arraySize, argumentTypes, argumentValues) ||
		!assignMaterializedInteger(result.matrixRows, matrixRows, argumentTypes, argumentValues) ||
		!assignMaterializedInteger(result.matrixColumns, matrixColumns, argumentTypes, argumentValues))
		return std::nullopt;
	if (elementConstraint) {
		std::optional<TypeConstraint> element = elementConstraint->materialize(argumentTypes, argumentValues);
		if (!element)
			return std::nullopt;
		result.elementConstraint = std::make_shared<TypeConstraint>(std::move(*element));
	}
	return result;
}
