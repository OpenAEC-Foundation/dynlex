#pragma once
#ifdef PEXLIT_GLM
#include "debugViewMode.h"
#include "debugWireframeMode.h"
#include "frustum.h"
#include "transparentSphereRenderData.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <vector>

struct VegetationSystem;

// Maximum render distance before scaling kicks in (for depth buffer precision)
constexpr double maxRenderDistance = 0b10000000000000000;

// Compute scale factor to keep objects within depth buffer precision range.
// Returns scale factor (1.0 if close enough, < 1.0 if far away).
inline double computeDistanceScale(double distance) {
	return (distance > maxRenderDistance) ? maxRenderDistance / distance : 1.0;
}

// Compute scaled position and scale factor for rendering distant objects.
// Both the position and any sizes should be multiplied by the returned scale.
inline double computeScaledPosition(const glm::dvec3 &relativePos, glm::vec3 &outScaledPos) {
	double distance = glm::length(relativePos);
	double scale = computeDistanceScale(distance);
	outScaledPos = glm::vec3(relativePos * scale);
	return scale;
}

// Shared data for rendering from the camera's point of view.
struct RenderContext {
	glm::mat4 worldToScreen;
	glm::dvec3 cameraPosition;
	Frustum frustum;
	float time{};
	float screenHeight{};
	float fovY{};
	float nearPlane{0.1f}; // Near plane distance for depth linearization
	DebugViewMode debugViewMode{};
	bool wireframePass{};
	bool showStarChunks{};
	VegetationSystem *vegetationSystem{};
	unsigned int sceneDepthTexture{}; // Scene depth texture for water/atmosphere shaders
	glm::ivec2 screenSize{};		  // Screen dimensions for depth texture sampling

	// Deferred transparent sphere rendering - collected during opaque pass, rendered later
	mutable DeferredTransparentSpheres deferredTransparentSpheres;

	glm::mat4 getBodyTransform(const glm::dvec3 &bodyPosition) const {
		glm::vec3 localPos = glm::vec3(bodyPosition - cameraPosition);
		glm::mat4 translateMat = glm::translate(glm::mat4(1.0f), localPos);
		return worldToScreen * translateMat;
	}
};
#endif
