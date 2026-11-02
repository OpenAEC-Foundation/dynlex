#pragma once
#include "typeConstraint.h"
#include "typeReferenceValue.h"
#include <compare>
#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

struct ExpressionCaptureData;

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

using CompileTimeValue = std::variant<
	std::monostate, std::int64_t, std::uint64_t, MinimumSignedIntegerMagnitude, double, std::string, bool, TypeReferenceValue,
	TypeConstraint, ExpressionCapture, ArgumentPackValue>;
