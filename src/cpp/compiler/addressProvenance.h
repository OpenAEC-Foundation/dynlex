#pragma once

#include "copyOnWrite.h"
#include <unordered_map>
#include <unordered_set>

struct VariableReference;

struct AddressProvenance {
	std::unordered_set<VariableReference *> mayTargets;
	bool unknown = false;

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
