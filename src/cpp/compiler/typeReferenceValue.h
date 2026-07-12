#pragma once

#include "typeConstraint.h"

struct TypeReferenceValue {
	DataType type;
	TypeConstraint constraint;

	static TypeReferenceValue exact(DataType typeReference) {
		requireCompilerInvariant(typeReference.kind == DataType::Kind::Type, "Type-reference value requires Type metadata");
		return {typeReference, TypeConstraint::fromTypeReference(typeReference)};
	}

	static TypeReferenceValue
	builtin(std::string_view kindName, bool emitSPIRV, std::optional<int> numericByteSize = std::nullopt) {
		std::optional<DataType> typeReference = makeBuiltinTypeReference(kindName, emitSPIRV, numericByteSize);
		requireCompilerInvariant(typeReference.has_value(), "Unknown built-in type-reference kind");
		TypeConstraint constraint = TypeConstraint::fromTypeReference(*typeReference);
		if (!numericByteSize &&
			(typeReference->referencedKind == DataType::Kind::Int || typeReference->referencedKind == DataType::Kind::Float)) {
			constraint.numericSize.reset();
		}
		return {*typeReference, std::move(constraint)};
	}

	bool operator==(const TypeReferenceValue &) const = default;
	bool operator<(const TypeReferenceValue &other) const {
		if (type != other.type)
			return type < other.type;
		return constraint < other.constraint;
	}
};
