#pragma once
#ifdef PEXLIT_GL
#include "shader.h"
#include <Uniform.h>
#include <map>
#include <unordered_map>
struct ShaderProgram {
	GLuint ID{};
	Shader vertexShader;
	Shader fragmentShader;
	std::map<GLint, Uniform> uniforms;
	std::unordered_map<std::string, Uniform> optimizedAwayUniforms;
	ShaderProgram(Shader vertexShader, Shader fragmentShader);
	~ShaderProgram();
	ShaderProgram(const ShaderProgram &) = delete;
	ShaderProgram &operator=(const ShaderProgram &) = delete;
	void enable();
	Uniform *getUniform(std::string name);

  private:
	void findOptimizedAwayUniforms(const std::string &source);
};
#endif