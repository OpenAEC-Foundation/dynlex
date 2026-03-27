#pragma once
#ifdef PEXLIT_GLM
#include "plane.h"
#include <glm/mat3x3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

enum class FrustumPlane : int { Left = 0, Right, Bottom, Top, Near, Far, Count };

// View frustum for culling, extracted from view-projection matrix.
// Uses 6 planes: left, right, bottom, top, near, far.
struct Frustum {
	Plane planes[(int)FrustumPlane::Count]{};

	constexpr Frustum() = default;

	// Extract frustum planes from a view-projection matrix.
	// Planes face inward (positive side is inside frustum).
	// This version is for reverse-Z with infinite far plane (GL_ZERO_TO_ONE clip control).
	explicit Frustum(const glm::mat4 &viewProj) {
		planes[(int)FrustumPlane::Left].normal =
			glm::vec3(viewProj[0][3] + viewProj[0][0], viewProj[1][3] + viewProj[1][0], viewProj[2][3] + viewProj[2][0]);
		planes[(int)FrustumPlane::Left].distance = viewProj[3][3] + viewProj[3][0];

		planes[(int)FrustumPlane::Right].normal =
			glm::vec3(viewProj[0][3] - viewProj[0][0], viewProj[1][3] - viewProj[1][0], viewProj[2][3] - viewProj[2][0]);
		planes[(int)FrustumPlane::Right].distance = viewProj[3][3] - viewProj[3][0];

		planes[(int)FrustumPlane::Bottom].normal =
			glm::vec3(viewProj[0][3] + viewProj[0][1], viewProj[1][3] + viewProj[1][1], viewProj[2][3] + viewProj[2][1]);
		planes[(int)FrustumPlane::Bottom].distance = viewProj[3][3] + viewProj[3][1];

		planes[(int)FrustumPlane::Top].normal =
			glm::vec3(viewProj[0][3] - viewProj[0][1], viewProj[1][3] - viewProj[1][1], viewProj[2][3] - viewProj[2][1]);
		planes[(int)FrustumPlane::Top].distance = viewProj[3][3] - viewProj[3][1];

		// For reverse-Z with GL_ZERO_TO_ONE: near is at z_ndc=1, far is at z_ndc=0
		// Standard extraction assumes near at z_ndc=0 and far at z_ndc=1, so swap the formulas.
		// Near plane: use standard far formula (row3 - row2) for z_ndc <= 1
		planes[(int)FrustumPlane::Near].normal =
			glm::vec3(viewProj[0][3] - viewProj[0][2], viewProj[1][3] - viewProj[1][2], viewProj[2][3] - viewProj[2][2]);
		planes[(int)FrustumPlane::Near].distance = viewProj[3][3] - viewProj[3][2];

		// Far plane: for infinite far projection, the far plane is at infinity.
		// Use standard near formula (row2 only for GL_ZERO_TO_ONE) for z_ndc >= 0
		// This will be a degenerate/always-pass plane for infinite far, which is correct.
		planes[(int)FrustumPlane::Far].normal = glm::vec3(viewProj[0][2], viewProj[1][2], viewProj[2][2]);
		planes[(int)FrustumPlane::Far].distance = viewProj[3][2];

		// Normalize planes
		for (Plane &plane : planes) {
			float length = glm::length(plane.normal);
			if (length > 0.0001f) {
				plane.normal /= length;
				plane.distance /= length;
			} else {
				// Degenerate plane (infinite far) - make it always pass
				plane.normal = glm::vec3(0.0f);
				plane.distance = 1.0f;
			}
		}
	}

	// Test if a sphere intersects or is inside the frustum.
	constexpr bool containsSphere(glm::vec3 center, float radius) const {
		for (const Plane &plane : planes) {
			if (plane.signedDistance(center) < -radius)
				return false;
		}
		return true;
	}

	// Test if an axis-aligned box intersects or is inside the frustum.
	constexpr bool containsBox(glm::vec3 center, float extent) const {
		for (const Plane &plane : planes) {
			// Find the corner most in the direction of the plane normal
			float effectiveRadius = extent * (std::abs(plane.normal.x) + std::abs(plane.normal.y) + std::abs(plane.normal.z));
			if (plane.signedDistance(center) < -effectiveRadius)
				return false;
		}
		return true;
	}

	// Test if an axis-aligned box with extended radius intersects the frustum.
	// Useful for chunks where content can extend beyond chunk bounds.
	constexpr bool containsBoxExtended(glm::vec3 center, float extent, float extension) const {
		for (const Plane &plane : planes) {
			float effectiveRadius = extent * (std::abs(plane.normal.x) + std::abs(plane.normal.y) + std::abs(plane.normal.z));
			if (plane.signedDistance(center) < -(effectiveRadius + extension))
				return false;
		}
		return true;
	}

	// Transform frustum to a local coordinate system.
	// invRotation: inverse rotation (transpose for orthogonal matrices) from world to local
	// offset: translation from camera to local origin in world space (localOrigin - cameraPosition)
	// Returns a new frustum that can test points in local space directly.
	Frustum transformToLocal(const glm::mat3 &invRotation, const glm::dvec3 &offset) const {
		Frustum local;
		for (int i = 0; i < (int)FrustumPlane::Count; ++i) {
			// Transform normal to local space
			local.planes[i].normal = invRotation * planes[i].normal;
			// Adjust distance: d_local = d_world + dot(n_world, offset)
			local.planes[i].distance = planes[i].distance + (float)glm::dot(glm::dvec3(planes[i].normal), offset);
		}
		return local;
	}
};
#endif
