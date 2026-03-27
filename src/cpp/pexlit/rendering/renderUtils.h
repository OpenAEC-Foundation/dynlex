#pragma once
#ifdef PEXLIT_GL
#include "pexlit/safeInclude/includeGL.h"
#include <cmath>
#include <glm/glm.hpp>

// Shared rendering utilities for reversed-Z depth buffer.
// Both the universe simulator (program.cpp) and model viewer use these to avoid duplicate code.

// Reversed-Z infinite perspective matrix for better depth precision at large distances.
// Maps near plane to 1.0, infinity to 0.0. Requires glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE).
constexpr glm::mat4 infinitePerspectiveReverseZ(float fovY, float aspect, float zNear) {
	float f = 1.0f / std::tan(fovY / 2.0f);
	glm::mat4 result(0.0f);
	result[0][0] = f / aspect;
	result[1][1] = f;
	result[2][3] = -1.0f; // w = -z
	result[3][2] = zNear;
	return result;
}

// Prepare for rendering with reversed-Z depth buffer.
// Must be called before glClear() to ensure depth buffer is properly cleared.
// Depth mask must be enabled for glClear to actually clear the depth buffer.
inline void prepareDepthClear() {
	glClearDepth(0.0); // Reversed-Z: far plane is 0.0
	glDepthMask(GL_TRUE);
}

// One-time setup for reversed-Z rendering. Call once during initialization.
inline void initReversedZ() { glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE); }
#endif
