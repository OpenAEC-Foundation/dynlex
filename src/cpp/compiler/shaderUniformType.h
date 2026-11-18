#pragma once
#include "type.h"
#include <cstdint>
#include <optional>

struct ShaderUniformLayout {
	const char *scalar;
	std::uint32_t rows;
	std::uint32_t columns;
};

inline std::optional<ShaderUniformLayout> shaderUniformLayout(const DataType &type) {
	if (type.isPointer())
		return std::nullopt;
	DataType scalar = type;
	std::uint32_t rows = 1, columns = 1;
	if (type.kind == DataType::Kind::Vector) {
		if (type.arraySize < 2 || type.arraySize > 4)
			return std::nullopt;
		columns = type.arraySize;
		scalar = type.aggregateElementType();
	} else if (type.kind == DataType::Kind::Matrix) {
		if (type.matrixRows() < 2 || type.matrixRows() > 4 || type.matrixColumns() < 2 || type.matrixColumns() > 4)
			return std::nullopt;
		rows = type.matrixRows();
		columns = type.matrixColumns();
		scalar = type.matrixElementType();
		if (scalar.kind != DataType::Kind::Float)
			return std::nullopt;
	}
	if (scalar.isPointer() || scalar.numericSize != 4)
		return std::nullopt;
	const char *name = scalar.kind == DataType::Kind::Float	 ? "float32"
					   : scalar.kind == DataType::Kind::Int	 ? "sint32"
					   : scalar.kind == DataType::Kind::UInt ? "uint32"
															 : nullptr;
	if (!name)
		return std::nullopt;
	return ShaderUniformLayout{name, rows, columns};
}
