#pragma once
#ifdef PEXLIT_GLM
#include <cstdint>
#include <glm/vec3.hpp>
#include <random>

inline std::uint32_t hashSeed(std::uint32_t seed) {
	seed += 0x9E3779B9u;
	seed ^= seed >> 16;
	seed *= 0x85EBCA6Bu;
	seed ^= seed >> 13;
	seed *= 0xC2B2AE35u;
	seed ^= seed >> 16;
	return seed;
}

inline glm::dvec3 randomPointOnSphere(std::mt19937 &random, double radius) {
	std::normal_distribution<double> distribution{};
	glm::dvec3 v{distribution(random), distribution(random), distribution(random)};
	return glm::normalize(v) * radius;
}

inline glm::dvec3 randomPointInUnitSphere(std::mt19937 &random) {
	std::uniform_real_distribution<double> dist(-1.0, 1.0);
	for (;;) {
		glm::dvec3 p{dist(random), dist(random), dist(random)};
		if (glm::dot(p, p) <= 1.0)
			return p;
	}
}

inline glm::dvec3 randomPointInEllipsoid(std::mt19937 &random, double radius, double thickness) {
	glm::dvec3 p = randomPointInUnitSphere(random);
	return glm::dvec3(p.x * radius, p.y * thickness, p.z * radius);
}
#endif
