#pragma once
#include <memory>

struct ClassInstantiationRequest;

struct ClassInstantiationRequestRecord {
	int index;
	// Invocation-local identities have no retainable snapshot inputs.
	std::shared_ptr<const ClassInstantiationRequest> persistent;
};
