#pragma once

#include "compilerUtils.h"
#include <memory>
#include <utility>

template <typename Storage> class CopyOnWrite {
  public:
	CopyOnWrite() : storage(std::make_shared<Storage>()) {}
	explicit CopyOnWrite(Storage initialStorage) : storage(std::make_shared<Storage>(std::move(initialStorage))) {}

	const Storage &read() const {
		requireCompilerInvariant(storage != nullptr, "read from moved-from copy-on-write state");
		return *storage;
	}

	Storage &write() {
		requireCompilerInvariant(storage != nullptr, "write to moved-from copy-on-write state");
		if (!storage.unique())
			storage = std::make_shared<Storage>(*storage);
		return *storage;
	}

	void reset() {
		requireCompilerInvariant(storage != nullptr, "reset moved-from copy-on-write state");
		storage = std::make_shared<Storage>();
	}

	bool operator==(const CopyOnWrite &other) const {
		requireCompilerInvariant(storage != nullptr, "compare moved-from copy-on-write state");
		requireCompilerInvariant(other.storage != nullptr, "compare against moved-from copy-on-write state");
		return storage == other.storage || *storage == *other.storage;
	}

  private:
	std::shared_ptr<Storage> storage;
};
