#ifdef PEXLIT_GL
#include "shaderProgram.h"
#include <iostream>
#include <regex>

ShaderProgram::ShaderProgram(Shader vertexShader, Shader fragmentShader)
	: vertexShader(vertexShader), fragmentShader(fragmentShader) {
	ID = glCreateProgram();
	glAttachShader(ID, vertexShader.ID);
	glAttachShader(ID, fragmentShader.ID);
	glLinkProgram(ID);

	GLint count;
	glGetProgramiv(ID, GL_ACTIVE_UNIFORMS, &count);

	const GLsizei bufSize = 64; // maximum name length
	GLchar nameBuffer[bufSize]; // variable name in GLSL
	GLsizei nameLength;			// name length

	for (int i = 0; i < count; i++) {
		Uniform uniform{};
		uniform.shaderProgramID = ID;
		glGetActiveUniform(ID, (GLuint)i, bufSize, &nameLength, &uniform.size, &uniform.type, nameBuffer);
		uniform.name = std::string(nameBuffer, nameLength);

		// Strip [0] suffix from array uniforms
		if (uniform.name.ends_with("[0]"))
			uniform.name = uniform.name.substr(0, uniform.name.size() - 3);

		uniform.location = glGetUniformLocation(ID, uniform.name.c_str());
		uniforms[uniform.location] = uniform;
	}

	// Find uniforms declared in source but optimized away
	findOptimizedAwayUniforms(vertexShader.source);
	findOptimizedAwayUniforms(fragmentShader.source);
}

void ShaderProgram::findOptimizedAwayUniforms(const std::string &source) {
	// Match "uniform <type> <name>" declarations
	std::regex pattern(R"(uniform\s+\w+\s+(\w+))");
	std::sregex_iterator it(source.begin(), source.end(), pattern);
	std::sregex_iterator end;

	for (; it != end; ++it) {
		std::string name = (*it)[1].str();

		// Check if already in active uniforms
		GLint location = glGetUniformLocation(ID, name.c_str());
		if (location == -1 && optimizedAwayUniforms.find(name) == optimizedAwayUniforms.end()) {
			// Uniform was declared but optimized away
			optimizedAwayUniforms[name] = Uniform{.location = -1, .shaderProgramID = ID, .size = 0, .name = name, .type = 0};
		}
	}
}

void ShaderProgram::enable() { glUseProgram(ID); }

ShaderProgram::~ShaderProgram() {
	if (ID) {
		glDetachShader(ID, vertexShader.ID);
		glDetachShader(ID, fragmentShader.ID);
		glDeleteShader(vertexShader.ID);
		glDeleteShader(fragmentShader.ID);
		glDeleteProgram(ID);
	}
}

Uniform *ShaderProgram::getUniform(std::string name) {
	GLint location = glGetUniformLocation(ID, name.c_str());
	if (location != -1) {
		auto it = uniforms.find(location);
		if (it != uniforms.end())
			return &it->second;
	}

	// Check if uniform was declared but optimized away
	auto it = optimizedAwayUniforms.find(name);
	if (it != optimizedAwayUniforms.end()) {
		return &it->second;
	}

	std::string message = "uniform " + name + " not declared in shader";
#ifdef NDEBUG
	std::cout << message << std::endl;
	return nullptr;
#else
	throw std::runtime_error(message);
#endif
}
#endif