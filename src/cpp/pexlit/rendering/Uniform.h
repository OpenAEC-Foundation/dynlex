#pragma once
#ifdef PEXLIT_GL
#include "safeInclude/includeGL.h"
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>
#include <span>
#include <type_traits>
#include <vector>

struct Uniform {
	// location in the shader (-1 if optimized away)
	GLint location;
	GLuint shaderProgramID;
	GLint size;
	std::string name;
	// float, vec3 or mat4, etc
	GLenum type;
	template <typename ValueType> void setValue(const ValueType &newValue) {
		if (location == -1)
			return; // Uniform was optimized away
		if constexpr (std::is_same_v<ValueType, glm::vec4>) {
			glUniform4fv(location, 1, &newValue[0]);
		} else if constexpr (std::is_same_v<ValueType, glm::vec3>) {
			glUniform3fv(location, 1, &newValue[0]);
		} else if constexpr (std::is_same_v<ValueType, glm::vec2>) {
			glUniform2fv(location, 1, &newValue[0]);
		} else if constexpr (std::is_same_v<ValueType, int>) {
			glUniform1i(location, newValue);
		} else if constexpr (std::is_same_v<ValueType, bool>) {
			glUniform1i(location, newValue ? 1 : 0);
		} else if constexpr (std::is_same_v<ValueType, glm::mat4x4>) {
			glUniformMatrix4fv(location, 1, false, &newValue[0][0]);
		} else if constexpr (std::is_same_v<ValueType, float>) {
			glUniform1fv(location, 1, &newValue);
		} else if constexpr (std::is_enum_v<ValueType>) {
			glUniform1i(location, static_cast<int>(newValue));
		} else if constexpr (std::is_same_v<ValueType, std::span<glm::mat4>>) {
			glUniformMatrix4fv(location, (GLsizei)newValue.size(), GL_FALSE, &newValue[0][0][0]);
		} else if constexpr (std::is_same_v<ValueType, std::vector<glm::mat4>>) {
			glUniformMatrix4fv(location, (GLsizei)newValue.size(), GL_FALSE, &newValue[0][0][0]);
		} else {
			static_assert(false, "type not implemented");
		}
	}
};
#endif
