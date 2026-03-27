#pragma once
#ifdef PEXLIT_GLM
#include <cmath>
#include <glm/vec3.hpp>

// Convert HSV (all components 0-1) to RGB
inline glm::vec3 hsvToRgb(float hue, float saturation, float value) {
	float h = hue * 6.0f;
	float c = value * saturation;
	float x = c * (1.0f - std::abs(std::fmod(h, 2.0f) - 1.0f));
	float m = value - c;

	glm::vec3 rgb;
	if (h < 1.0f)
		rgb = glm::vec3(c, x, 0.0f);
	else if (h < 2.0f)
		rgb = glm::vec3(x, c, 0.0f);
	else if (h < 3.0f)
		rgb = glm::vec3(0.0f, c, x);
	else if (h < 4.0f)
		rgb = glm::vec3(0.0f, x, c);
	else if (h < 5.0f)
		rgb = glm::vec3(x, 0.0f, c);
	else
		rgb = glm::vec3(c, 0.0f, x);

	return rgb + glm::vec3(m);
}
#endif
