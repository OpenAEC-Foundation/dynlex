#pragma once
#ifdef PEXLIT_GL
#include "safeInclude/includeGL.h"
#include <filesystem>
#include <string>
// the type of a shader (fragment or vertex)
enum class ShaderType { Fragment = GL_FRAGMENT_SHADER, Vertex = GL_VERTEX_SHADER };
struct Shader {
	Shader(const std::filesystem::path &path);
	Shader(Shader &&other) = default;
	Shader(Shader &other) = default;
	GLuint ID;
	// GL_FRAGMENT_SHADER or GL_VERTEX_SHADER
	ShaderType type;
	// wether the shader compiled correctly
	bool success;
	// preprocessed source code (for uniform declaration checking)
	std::string source;
};
#endif