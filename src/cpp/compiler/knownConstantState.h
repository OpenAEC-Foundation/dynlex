#pragma once

#include "compileTimeInfo.h"
#include "copyOnWrite.h"
#include "variableStorageKey.h"
#include <unordered_map>
#include <utility>

struct VariableReference;

using KnownConstantStorage = std::unordered_map<VariableStorageKey, CompileTimeValue, VariableStorageKeyHash>;

class KnownConstantState {
  public:
	KnownConstantState() = default;
	explicit KnownConstantState(KnownConstantStorage storage) : values(std::move(storage)) {}

	const KnownConstantStorage &read() const { return values.read(); }
	KnownConstantStorage &write() { return values.write(); }
	void clear() { values.reset(); }

	bool operator==(const KnownConstantState &) const = default;

  private:
	CopyOnWrite<KnownConstantStorage> values;
};
