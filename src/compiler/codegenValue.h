#pragma once
#include "range.h"
#include <cstdint>
#include <string>
#include <variant>

struct PatternReference;
struct VariableReference;

struct CodegenValue {
	enum class SourceType { Result, Literal, Variable };

	enum class Type { Undeduced, Integer, Float, String };

	SourceType sourceType;
	Type type = Type::Undeduced;
	Range range;

	std::variant<std::monostate, int64_t, double, std::string, VariableReference *, PatternReference *> value;

	template <typename T> static CodegenValue create(T val, Range range = {}) {
		CodegenValue v;
		v.range = range;
		v.value = val;

		if constexpr (std::is_integral_v<T>) {
			v.sourceType = SourceType::Literal;
			v.type = Type::Integer;
		} else if constexpr (std::is_floating_point_v<T>) {
			v.sourceType = SourceType::Literal;
			v.type = Type::Float;
		} else if constexpr (std::is_same_v<T, std::string>) {
			v.sourceType = SourceType::Literal;
			v.type = Type::String;
		} else if constexpr (std::is_same_v<T, VariableReference *>) {
			v.sourceType = SourceType::Variable;
		} else if constexpr (std::is_same_v<T, PatternReference *>) {
			v.sourceType = SourceType::Result;
		}

		return v;
	}
};
