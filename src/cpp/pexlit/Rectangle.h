#pragma once
#ifdef PEXLIT_GLM
#include "glm/glm.hpp"

template <typename T = float, int L = 2> struct Rect {
	typedef glm::vec<L, T> Vec;
	// the bottomleft position
	Vec pos00;
	// the size
	Vec size;
	constexpr Rect(Vec pos00 = {}, Vec size = {}) : pos00(pos00), size(size) {}
	constexpr bool contains(const Vec &pos) {
		for (int i = 0; i < 2; i++) {
			if (pos[i] < pos00[i] || pos[i] > pos00[i] + size[i])
				return false;
		}
		return true;
	}
	constexpr Rect expanded(const T &radius) const { return Rect(pos00 - radius, size + radius * 2.f); }
	constexpr static Rect fromTwoCorners(std::array<Vec, 2> corners) {
		Rect r = Rect();
		for (int i = 0; i < L; i++) {
			int lessIndex = corners[0][i] > corners[1][i];
			r.pos00[i] = corners[lessIndex][i];
			r.size[i] = corners[1 - lessIndex][i] - corners[lessIndex][i];
		}
		return r;
	}
};
typedef Rect<float, 2> Rect2;
#endif