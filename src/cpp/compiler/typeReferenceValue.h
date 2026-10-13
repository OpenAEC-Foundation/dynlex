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
		if (!numericByteSize && (kindName == "int" || kindName == "uint" || kindName == "float" || kindName == "integer")) {
			constraint.numericSize.reset();
		}
		// An integer leaves its sign unspecified; as an exact type it is signed by default.
		if (kindName == "integer") {
			constraint.kind.reset();
			constraint.requiresInteger = true;
		}
		return {*typeReference, std::move(constraint)};
	}

	// The type a constraint determines, completing an unspecified sign and width with the built-in defaults.
	static std::optional<TypeReferenceValue> forConstraint(const TypeConstraint &constraint, bool emitSPIRV) {
		std::optional<DataType> exactType = constraint.exactValueType();
		if (!exactType) {
			const bool floatingPoint = constraint.kind && *constraint.kind == DataType::Kind::Float;
			const bool integer =
				constraint.requiresInteger ||
				(constraint.kind && (*constraint.kind == DataType::Kind::Int || *constraint.kind == DataType::Kind::UInt));
			if (!floatingPoint && !integer)
				return std::nullopt;
			DataType type{constraint.kind.value_or(DataType::Kind::Int)};
			type.pointerDepth = constraint.pointerDepth.value_or(0);
			type.numericSize = constraint.numericSize.value_or(floatingPoint ? defaultFloatByteSize(emitSPIRV) : 4);
			exactType = type;
		}
		return TypeReferenceValue{exactType->asTypeReference(), constraint};
	}

	bool operator==(const TypeReferenceValue &) const = default;
	bool operator<(const TypeReferenceValue &other) const {
		if (type != other.type)
			return type < other.type;
		return constraint < other.constraint;
	}
};
