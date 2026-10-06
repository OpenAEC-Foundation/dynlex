#pragma once

#include "intrinsicInfo.h"
#include <optional>
#include <string_view>

enum class AtomicMemoryOrder { Relaxed, Acquire, Release, AcquireRelease, SequentiallyConsistent };

constexpr bool isAtomicIntrinsicKind(IntrinsicKind kind) {
	return kind == IntrinsicKind::AtomicLoad || kind == IntrinsicKind::AtomicStore || kind == IntrinsicKind::AtomicExchange ||
		   kind == IntrinsicKind::AtomicFetchAdd || kind == IntrinsicKind::AtomicFetchSub;
}

inline std::optional<AtomicMemoryOrder> parseAtomicMemoryOrder(std::string_view text) {
	if (text == "relaxed")
		return AtomicMemoryOrder::Relaxed;
	if (text == "acquire")
		return AtomicMemoryOrder::Acquire;
	if (text == "release")
		return AtomicMemoryOrder::Release;
	if (text == "acq_rel")
		return AtomicMemoryOrder::AcquireRelease;
	if (text == "seq_cst")
		return AtomicMemoryOrder::SequentiallyConsistent;
	return std::nullopt;
}

constexpr bool atomicOrderIsValidFor(IntrinsicKind kind, AtomicMemoryOrder order) {
	if (kind == IntrinsicKind::AtomicLoad)
		return order != AtomicMemoryOrder::Release && order != AtomicMemoryOrder::AcquireRelease;
	if (kind == IntrinsicKind::AtomicStore)
		return order != AtomicMemoryOrder::Acquire && order != AtomicMemoryOrder::AcquireRelease;
	return true;
}
