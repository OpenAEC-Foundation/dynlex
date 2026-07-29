#pragma once
#include "typeConstraint.h"
#include "typeReferenceValue.h"
#include <algorithm>
#include <compare>
#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

struct MinimumSignedIntegerMagnitudeIdentity {};

struct MinimumSignedIntegerMagnitude {
	std::shared_ptr<const MinimumSignedIntegerMagnitudeIdentity> identity;

	auto operator<=>(const MinimumSignedIntegerMagnitude &) const = default;
};

using CompileTimeValue = std::variant<
	std::monostate, std::int64_t, MinimumSignedIntegerMagnitude, double, std::string, bool, TypeReferenceValue, TypeConstraint>;

struct MinimumSignedIntegerMagnitudeEffects {
	std::vector<std::shared_ptr<const MinimumSignedIntegerMagnitudeIdentity>> consumedByNegation;
	std::vector<std::shared_ptr<const MinimumSignedIntegerMagnitudeIdentity>> rejectedUses;

	auto operator<=>(const MinimumSignedIntegerMagnitudeEffects &) const = default;
};

struct CompileTimeEvaluation {
	CompileTimeValue value;
	MinimumSignedIntegerMagnitudeEffects minimumIntegerEffects;
};

inline void mergeMinimumSignedIntegerMagnitudeIdentities(
	std::vector<std::shared_ptr<const MinimumSignedIntegerMagnitudeIdentity>> &destination,
	const std::vector<std::shared_ptr<const MinimumSignedIntegerMagnitudeIdentity>> &source
) {
	for (const auto &identity : source) {
		if (std::ranges::find(destination, identity) == destination.end())
			destination.push_back(identity);
	}
}

inline void mergeMinimumSignedIntegerMagnitudeEffects(
	MinimumSignedIntegerMagnitudeEffects &destination, const MinimumSignedIntegerMagnitudeEffects &source
) {
	mergeMinimumSignedIntegerMagnitudeIdentities(destination.consumedByNegation, source.consumedByNegation);
	mergeMinimumSignedIntegerMagnitudeIdentities(destination.rejectedUses, source.rejectedUses);
}

inline bool containsMinimumSignedIntegerMagnitudeIdentity(
	const std::vector<std::shared_ptr<const MinimumSignedIntegerMagnitudeIdentity>> &identities,
	const std::shared_ptr<const MinimumSignedIntegerMagnitudeIdentity> &identity
) {
	return std::ranges::find(identities, identity) != identities.end();
}
