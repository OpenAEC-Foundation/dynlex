#pragma once

#include <compare>
#include <functional>
#include <memory>

struct VariableReference;
// One lexical invocation, stable across inference passes and body recreation.
struct VariableStorageScope {};
struct VariableStorageKey {
	VariableReference *definition{};
	std::shared_ptr<const VariableStorageScope> scope{};

	auto operator<=>(const VariableStorageKey &) const = default;
};

struct VariableStorageKeyHash {
	size_t operator()(const VariableStorageKey &key) const {
		return std::hash<VariableReference *>{}(key.definition) ^
			   (std::hash<std::shared_ptr<const VariableStorageScope>>{}(key.scope) << 1);
	}
};
