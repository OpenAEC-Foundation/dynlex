#pragma once

#include <memory>
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
	AddressInferenceState() : storage(std::make_unique<AddressInferenceStorage>()) {}
	AddressInferenceState(const AddressInferenceState &other)
		: storage(std::make_unique<AddressInferenceStorage>(*other.storage)) {}
	AddressInferenceState(AddressInferenceState &&) noexcept = default;
	AddressInferenceState &operator=(const AddressInferenceState &other) {
		if (this != &other)
			*storage = *other.storage;
		return *this;
	}
	AddressInferenceState &operator=(AddressInferenceState &&) noexcept = default;

	AddressInferenceStorage *operator->() { return storage.get(); }
	const AddressInferenceStorage *operator->() const { return storage.get(); }

	bool operator==(const AddressInferenceState &other) const { return *storage == *other.storage; }

  private:
	std::unique_ptr<AddressInferenceStorage> storage;
};
