#pragma once

#include "compileTimeInfo.h"
#include "typeConstraint.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

enum class ConstraintAccess {
	Type,
	CompileTimeValue,
};

struct ConstraintDependency {
	size_t sourceArgumentIndex{};
	ConstraintAccess access = ConstraintAccess::Type;

	auto operator<=>(const ConstraintDependency &) const = default;
};

struct ConstraintIntegerTerm {
	enum class Kind {
		Constant,
		TypeExtent,
		FixedArgument,
		Add,
		Subtract,
		Multiply,
		Divide,
		Modulo,
	};

	Kind kind = Kind::Constant;
	std::int64_t constant{};
	size_t sourceArgumentIndex{};
	int dimension{};
	std::shared_ptr<ConstraintIntegerTerm> left;
	std::shared_ptr<ConstraintIntegerTerm> right;

	bool operator==(const ConstraintIntegerTerm &other) const;

	static ConstraintIntegerTerm constantValue(std::int64_t value);
	static ConstraintIntegerTerm typeExtent(size_t argumentIndex, int dimension);
	static ConstraintIntegerTerm fixedArgument(size_t argumentIndex);
	static ConstraintIntegerTerm binary(Kind kind, ConstraintIntegerTerm left, ConstraintIntegerTerm right);

	std::optional<std::int64_t>
	materialize(const std::vector<DataType> &argumentTypes, const std::vector<CompileTimeValue> &argumentValues) const;
	void collectDependencies(std::vector<ConstraintDependency> &dependencies) const;
};

struct ConstraintTypeProjection {
	size_t sourceArgumentIndex{};
	size_t elementDepth{};

	auto operator<=>(const ConstraintTypeProjection &) const = default;
};

struct TypeConstraintTemplate {
	TypeConstraint constantPart = TypeConstraint::any();
	std::optional<ConstraintTypeProjection> rootProjection;
	std::optional<ConstraintIntegerTerm> numericSize;
	std::optional<ConstraintIntegerTerm> pointerDepth;
	std::optional<ConstraintIntegerTerm> arraySize;
	std::optional<ConstraintIntegerTerm> matrixRows;
	std::optional<ConstraintIntegerTerm> matrixColumns;
	std::shared_ptr<TypeConstraintTemplate> elementConstraint;
	std::vector<ConstraintDependency> dependencies;

	bool operator==(const TypeConstraintTemplate &other) const;

	static TypeConstraintTemplate constant(TypeConstraint constraint);
	static TypeConstraintTemplate projectedType(size_t argumentIndex, size_t elementDepth = 0);

	bool isDependent() const;
	TypeConstraint structuralEnvelope() const;
	void collectDependencies();
	std::optional<TypeConstraint>
	materialize(const std::vector<DataType> &argumentTypes, const std::vector<CompileTimeValue> &argumentValues) const;
};

struct PatternParameterSignature {
	size_t elementStartPos{};
	TypeConstraintTemplate constraint;
	DataType staticParameterType;
	bool requiresCompileTimeValue = false;
	bool acceptsUnresolvedType = false;
	bool hasExplicitTypeConstraint = false;
};

struct PatternPathSignature {
	std::vector<PatternParameterSignature> parameters;
};
