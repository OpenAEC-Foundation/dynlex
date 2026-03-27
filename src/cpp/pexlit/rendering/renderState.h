#pragma once
#ifdef PEXLIT_GL
#include "pexlit/safeInclude/includeGL.h"

struct RenderState {
	bool cullFace;
	bool cullBack{true}; // true=cull back faces (CCW front), false=cull front faces
	bool depthWrite;
	bool wireframe;
	bool blend;
	GLenum blendSrc{GL_ONE};
	GLenum blendDst{GL_ZERO};
	GLenum depthFunc{GL_GREATER}; // Reversed-Z for main scene; model viewer overrides to GL_LESS

	void apply() const {
		if (cullFace) {
			glEnable(GL_CULL_FACE);
			glCullFace(cullBack ? GL_BACK : GL_FRONT);
		} else
			glDisable(GL_CULL_FACE);

		// Always enable depth test - use GL_ALWAYS for depthFunc and depthWrite=false
		// to achieve "no depth testing" behavior. This is simpler than toggling depth test.
		glEnable(GL_DEPTH_TEST);
		glDepthMask(depthWrite ? GL_TRUE : GL_FALSE);
		glDepthFunc(depthFunc);
		glPolygonMode(GL_FRONT_AND_BACK, wireframe ? GL_LINE : GL_FILL);

		if (blend) {
			glEnable(GL_BLEND);
			glBlendFunc(blendSrc, blendDst);
		} else
			glDisable(GL_BLEND);
	}
};
#endif
