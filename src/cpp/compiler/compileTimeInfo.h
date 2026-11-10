#pragma once
#include "typeConstraint.h"
#include "typeReferenceValue.h"
#include "wideInteger.h"
#include <compare>
#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

struct ExpressionCaptureData;
struct PatternDefinition;
struct Instantiation;

struct ExpressionCapture {
	std::string identity;
	std::shared_ptr<const ExpressionCaptureData> data;
	bool operator==(const ExpressionCapture &other) const { return identity == other.identity; }
	auto operator<=>(const ExpressionCapture &other) const { return identity <=> other.identity; }
};

struct ArgumentPackValue {
	std::vector<ExpressionCapture> elements;
	auto operator<=>(const ArgumentPackValue &) const = default;
};

struct MinimumSignedIntegerMagnitudeIdentity {};

struct MinimumSignedIntegerMagnitude {
	std::shared_ptr<const MinimumSignedIntegerMagnitudeIdentity> identity;

	auto operator<=>(const MinimumSignedIntegerMagnitude &) const = default;
};

// Absolute pointer bits, normalized to the compilation target's pointer width.
// Symbolic addresses (such as string storage) retain their own value category.
struct CompileTimePointer {
	std::uint64_t address{};
	auto operator<=>(const CompileTimePointer &) const = default;
};

// A callable has a stable identity before linking assigns its address or table index.
struct CompileTimeFunction {
	PatternDefinition *definition{};
	size_t pathIndex{};
	Instantiation *instantiation{};
	auto operator<=>(const CompileTimeFunction &) const = default;
};

using CompileTimeValue = std::variant<
	std::monostate, std::int64_t, std::uint64_t, MinimumSignedIntegerMagnitude, double, std::string, bool, TypeReferenceValue,
	TypeConstraint, CompileTimePointer, CompileTimeFunction, ExpressionCapture, ArgumentPackValue, WideInteger>;
