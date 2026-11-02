#pragma once

#include "compileTimeInfo.h"
#include "copyOnWrite.h"
#include "type.h"
#include "variableStorageKey.h"
#include <map>
#include <set>
#include <vector>

struct Variable;

struct StorageTypeProjection {
	enum class Kind { PointerDepth, Field, Element };
	Kind kind;
	int index;
	auto operator<=>(const StorageTypeProjection &) const = default;
};

struct StorageTypeView {
	VariableStorageKey root;
	std::vector<StorageTypeProjection> projections;
	auto operator<=>(const StorageTypeView &) const = default;
};

// Assignments constrain storage types even after the copied runtime pointer
// changes. These are static type equations, independently of execution facts.
struct TypeRefinementStorage {
	struct Root {
		Variable *variable;
		DataType type;
	};
	std::map<VariableStorageKey, Root> roots;
	std::map<StorageTypeView, std::set<StorageTypeView>> equalViews;
	std::map<Expression *, ExpressionCapture> subjectSources;
};

struct TypeRefinementState {
	const TypeRefinementStorage &read() const { return value.read(); }
	TypeRefinementStorage &write() { return value.write(); }
	bool sharesStorageWith(const TypeRefinementState &other) const { return &read() == &other.read(); }

  private:
	CopyOnWrite<TypeRefinementStorage> value;
};
