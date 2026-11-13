#pragma once
#include <compare>
#include <cstdint>
#include <string>

// Static shader interface data, independent of a host GPU's runtime objects.
enum class ShaderResourceKind { Image2D, ImageCube, Depth2D, Sampler, ComparisonSampler };

struct ShaderResourceValue {
	std::string name;
	std::uint32_t binding;
	ShaderResourceKind kind;
	auto operator<=>(const ShaderResourceValue &) const = default;
	bool isImage() const {
		return kind == ShaderResourceKind::Image2D || kind == ShaderResourceKind::ImageCube ||
			   kind == ShaderResourceKind::Depth2D;
	}
	std::string identity() const {
		return std::to_string(static_cast<int>(kind)) + ":" + std::to_string(binding) + ":" + std::to_string(name.size()) +
			   ":" + name;
	}
};
