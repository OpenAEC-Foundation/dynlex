#pragma once
#ifdef PEXLIT_GLM
#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

// A plane in 3D space defined by normal and distance from origin.
// Points where dot(normal, point) + distance >= 0 are on the positive side.
struct Plane {
	glm::vec3 normal{};
	float distance{};

	constexpr Plane() = default;
	constexpr Plane(glm::vec3 normal, float distance) : normal(normal), distance(distance) {}

	// Signed distance from point to plane (positive = front side)
	constexpr float signedDistance(glm::vec3 point) const { return glm::dot(normal, point) + distance; }
};
#endif
