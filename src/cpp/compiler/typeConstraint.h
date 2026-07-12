#pragma once

#include "type.h"
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <tuple>

struct TypeConstraint {
	bool resolved = false;
	std::optional<DataType::Kind> kind;
	std::optional<int> numericSize;
	std::optional<int> pointerDepth;
	std::optional<int> arraySize;
	std::optional<int> matrixRows;
	std::optional<int> matrixColumns;
	bool constrainsClassDefinition = false;
	ClassDefinition *classDefinition = nullptr;
	std::optional<int> classInstantiationIndex;
	std::shared_ptr<TypeConstraint> elementConstraint;
	bool requiresCompileTimeValue = false;

	static TypeConstraint any() {
		TypeConstraint result;
		result.resolved = true;
		return result;
	}

	static TypeConstraint fromValueType(const DataType &type) {
		if (type.kind == DataType::Kind::Any)
			return any();
		requireCompilerInvariant(type.isDeduced(), "Cannot create a constraint from an unresolved value type");
		TypeConstraint result = any();
		result.kind = type.kind;
		result.pointerDepth = type.pointerDepth;
		if (type.kind == DataType::Kind::Int || type.kind == DataType::Kind::Float)
			result.numericSize = type.numericSize;
		if (type.kind == DataType::Kind::Array || type.kind == DataType::Kind::Vector) {
			result.arraySize = type.arraySize;
			if (type.arrayElementType)
				result.elementConstraint = std::make_shared<TypeConstraint>(fromValueType(*type.arrayElementType));
		}
		if (type.kind == DataType::Kind::Matrix) {
			result.matrixRows = type.matrixRowCount;
			result.matrixColumns = type.arraySize;
			if (type.arrayElementType)
				result.elementConstraint = std::make_shared<TypeConstraint>(fromValueType(*type.arrayElementType));
		}
		if (type.kind == DataType::Kind::Class) {
			result.constrainsClassDefinition = true;
			result.classDefinition = type.classDefinition;
			if (type.classInstIndex >= 0)
				result.classInstantiationIndex = type.classInstIndex;
		}
		return result;
	}

	static TypeConstraint fromTypeReference(const DataType &typeReference) {
		requireCompilerInvariant(typeReference.kind == DataType::Kind::Type, "Constraint source must be a type reference");
		TypeConstraint result = any();
		if (typeReference.referencedKind == DataType::Kind::Unresolved) {
			result.resolved = false;
			return result;
		}
		if (typeReference.referencedKind != DataType::Kind::Any)
			result.kind = typeReference.referencedKind;
		if (typeReference.numericSize > 0)
			result.numericSize = typeReference.numericSize;
		if (typeReference.referencedKind != DataType::Kind::Type &&
			typeReference.referencedKind != DataType::Kind::Constraint &&
			(typeReference.pointerDepth > 0 || typeReference.referencedKind != DataType::Kind::Any || result.numericSize))
			result.pointerDepth = typeReference.pointerDepth;
		if (typeReference.referencedKind == DataType::Kind::Array || typeReference.referencedKind == DataType::Kind::Vector) {
			result.arraySize = typeReference.arraySize;
			if (typeReference.arrayElementType)
				result.elementConstraint = std::make_shared<TypeConstraint>(fromValueType(*typeReference.arrayElementType));
		}
		if (typeReference.referencedKind == DataType::Kind::Matrix) {
			result.matrixRows = typeReference.matrixRowCount;
			result.matrixColumns = typeReference.arraySize;
			if (typeReference.arrayElementType)
				result.elementConstraint = std::make_shared<TypeConstraint>(fromValueType(*typeReference.arrayElementType));
		}
		if (typeReference.referencedKind == DataType::Kind::Class) {
			result.constrainsClassDefinition = true;
			result.classDefinition = typeReference.classDefinition;
			if (typeReference.classInstIndex >= 0)
				result.classInstantiationIndex = typeReference.classInstIndex;
		}
		return result;
	}

	bool isResolved() const { return resolved; }

	bool isStructurallyUnconstrained() const {
		return !kind && !numericSize && !pointerDepth && !arraySize && !matrixRows && !matrixColumns &&
			   !constrainsClassDefinition && !classInstantiationIndex && !elementConstraint;
	}

	bool accepts(const DataType &argumentType, bool compileTimeKnown) const {
		if (!resolved || !argumentType.isDeduced())
			return false;
		if (requiresCompileTimeValue && !compileTimeKnown)
			return false;
		if (kind && argumentType.kind != *kind)
			return false;
		if (numericSize && argumentType.numericSize != *numericSize)
			return false;
		if (pointerDepth && argumentType.pointerDepth != *pointerDepth)
			return false;
		if (arraySize && argumentType.arraySize != *arraySize)
			return false;
		if (matrixRows && argumentType.matrixRowCount != *matrixRows)
			return false;
		if (matrixColumns && argumentType.arraySize != *matrixColumns)
			return false;
		if (constrainsClassDefinition && argumentType.classDefinition != classDefinition)
			return false;
		if (classInstantiationIndex && argumentType.classInstIndex != *classInstantiationIndex)
			return false;
		if (elementConstraint) {
			if (!argumentType.arrayElementType || !elementConstraint->accepts(*argumentType.arrayElementType, true))
				return false;
		}
		return true;
	}

	bool structurallyOverlaps(const TypeConstraint &other) const {
		if (!resolved || !other.resolved)
			return true;
		auto incompatible = [](const auto &left, const auto &right) {
			return left.has_value() && right.has_value() && *left != *right;
		};
		if (incompatible(kind, other.kind) || incompatible(numericSize, other.numericSize) ||
			incompatible(pointerDepth, other.pointerDepth) || incompatible(arraySize, other.arraySize) ||
			incompatible(matrixRows, other.matrixRows) || incompatible(matrixColumns, other.matrixColumns) ||
			incompatible(classInstantiationIndex, other.classInstantiationIndex))
			return false;
		if (constrainsClassDefinition && other.constrainsClassDefinition && classDefinition != other.classDefinition)
			return false;
		if (elementConstraint && other.elementConstraint && !elementConstraint->structurallyOverlaps(*other.elementConstraint))
			return false;
		return true;
	}

	int structuralSpecificity() const {
		if (!resolved)
			return 0;
		int result = kind ? 4 : 0;
		result += numericSize ? 1 : 0;
		result += pointerDepth ? 1 : 0;
		result += arraySize ? 1 : 0;
		result += matrixRows ? 1 : 0;
		result += matrixColumns ? 1 : 0;
		result += constrainsClassDefinition ? 2 : 0;
		result += classInstantiationIndex ? 1 : 0;
		result += elementConstraint ? elementConstraint->structuralSpecificity() : 0;
		return result;
	}

	std::optional<DataType> exactValueType() const {
		if (!resolved || !kind || !pointerDepth)
			return std::nullopt;
		DataType result{*kind};
		result.pointerDepth = *pointerDepth;
		if (*kind == DataType::Kind::Int || *kind == DataType::Kind::Float) {
			if (!numericSize)
				return std::nullopt;
			result.numericSize = *numericSize;
		}
		if (*kind == DataType::Kind::Array || *kind == DataType::Kind::Vector) {
			if (!arraySize || !elementConstraint)
				return std::nullopt;
			std::optional<DataType> elementType = elementConstraint->exactValueType();
			if (!elementType)
				return std::nullopt;
			result.arraySize = *arraySize;
			result.arrayElementType = std::make_shared<DataType>(*elementType);
		}
		if (*kind == DataType::Kind::Matrix) {
			if (!matrixRows || !matrixColumns || !elementConstraint)
				return std::nullopt;
			std::optional<DataType> elementType = elementConstraint->exactValueType();
			if (!elementType)
				return std::nullopt;
			result.matrixRowCount = *matrixRows;
			result.arraySize = *matrixColumns;
			result.arrayElementType = std::make_shared<DataType>(*elementType);
		}
		if (*kind == DataType::Kind::Class) {
			if (!constrainsClassDefinition)
				return std::nullopt;
			result.classDefinition = classDefinition;
			result.classInstIndex = classInstantiationIndex.value_or(-1);
		}
		return result;
	}

	std::string toString() const {
		if (!resolved)
			return "unresolved constraint";
		std::string result = requiresCompileTimeValue ? "fixed " : "";
		if (!kind) {
			if (numericSize)
				return result + "a " + std::to_string(*numericSize * 8) + " bit any";
			return result + "any";
		}
		DataType displayType{*kind};
		displayType.numericSize = numericSize.value_or(0);
		displayType.pointerDepth = pointerDepth.value_or(0);
		displayType.arraySize = arraySize.value_or(matrixColumns.value_or(0));
		displayType.matrixRowCount = matrixRows.value_or(0);
		displayType.classDefinition = classDefinition;
		displayType.classInstIndex = classInstantiationIndex.value_or(-1);
		return result + displayType.toString();
	}

	bool samePayload(const TypeConstraint &other, bool includeCompileTimeRequirement) const {
		bool sameElement = (!elementConstraint && !other.elementConstraint) ||
						   (elementConstraint && other.elementConstraint &&
							elementConstraint->samePayload(*other.elementConstraint, includeCompileTimeRequirement));
		return resolved == other.resolved && kind == other.kind && numericSize == other.numericSize &&
			   pointerDepth == other.pointerDepth && arraySize == other.arraySize && matrixRows == other.matrixRows &&
			   matrixColumns == other.matrixColumns && constrainsClassDefinition == other.constrainsClassDefinition &&
			   classDefinition == other.classDefinition && classInstantiationIndex == other.classInstantiationIndex &&
			   sameElement && (!includeCompileTimeRequirement || requiresCompileTimeValue == other.requiresCompileTimeValue);
	}

	bool operator==(const TypeConstraint &other) const { return samePayload(other, true); }
	bool operator!=(const TypeConstraint &other) const { return !(*this == other); }
	bool operator<(const TypeConstraint &other) const {
		auto leftScalars = std::tie(
			resolved, kind, numericSize, pointerDepth, arraySize, matrixRows, matrixColumns, constrainsClassDefinition,
			classInstantiationIndex, requiresCompileTimeValue
		);
		auto rightScalars = std::tie(
			other.resolved, other.kind, other.numericSize, other.pointerDepth, other.arraySize, other.matrixRows,
			other.matrixColumns, other.constrainsClassDefinition, other.classInstantiationIndex, other.requiresCompileTimeValue
		);
		if (leftScalars != rightScalars)
			return leftScalars < rightScalars;
		if (classDefinition != other.classDefinition)
			return std::less<ClassDefinition *>{}(classDefinition, other.classDefinition);
		if (!!elementConstraint != !!other.elementConstraint)
			return !elementConstraint;
		return elementConstraint && *elementConstraint < *other.elementConstraint;
	}
};
