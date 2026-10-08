#pragma once

#include "copyOnWrite.h"
#include <unordered_map>
#include <unordered_set>

struct VariableReference;

struct AddressProvenance {
	std::unordered_set<VariableReference *> mayTargets;
	// A known null alternative is distinct from having no tracked addresses.
	bool mayBeNull = false;
	// Unknown origins can include null as well as untracked non-null addresses.
	bool unknown = false;
	// Pointer literals refer to static storage outside the tracked variables.
	bool mayBeStatic = false;

	bool operator==(const AddressProvenance &) const = default;
};

using VariableAddressProvenance = std::unordered_map<VariableReference *, AddressProvenance>;

struct AddressInferenceStorage {
	VariableAddressProvenance variables;
	std::unordered_set<VariableReference *> addressTakenVariables;
	AddressProvenance externallyEscaped;

	bool operator==(const AddressInferenceStorage &) const = default;
};

struct AddressInferenceState {
	AddressInferenceState() = default;

	const AddressInferenceStorage &read() const { return value.read(); }
	AddressInferenceStorage &write() { return value.write(); }

	bool operator==(const AddressInferenceState &) const = default;

  private:
	CopyOnWrite<AddressInferenceStorage> value;
};
