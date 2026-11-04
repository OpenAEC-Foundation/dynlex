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

// Absolute pointer bits, normalized to the compilation target's pointer width.
// Symbolic addresses (such as string storage) retain their own value category.
struct CompileTimePointer {
	std::uint64_t address{};
	auto operator<=>(const CompileTimePointer &) const = default;
};

using CompileTimeValue = std::variant<
	std::monostate, std::int64_t, std::uint64_t, MinimumSignedIntegerMagnitude, double, std::string, bool, TypeReferenceValue,
	TypeConstraint, CompileTimePointer>;
