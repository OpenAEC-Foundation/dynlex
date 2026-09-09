#pragma once
#include "typeConstraint.h"
#include "typeReferenceValue.h"
#include <compare>
#include <cstdint>
#include <memory>
#include <string>
#include <variant>

struct MinimumSignedIntegerMagnitudeIdentity {};

struct MinimumSignedIntegerMagnitude {
	std::shared_ptr<const MinimumSignedIntegerMagnitudeIdentity> identity;

	auto operator<=>(const MinimumSignedIntegerMagnitude &) const = default;
};

using CompileTimeValue = std::variant<
	std::monostate, std::int64_t, std::uint64_t, MinimumSignedIntegerMagnitude, double, std::string, bool, TypeReferenceValue,
	TypeConstraint>;
