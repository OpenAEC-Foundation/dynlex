#ifdef PEXLIT_GL
#include "shader.h"
#include "fileFunctions.h"
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>

static std::string preprocessShaderSource(const std::filesystem::path &path) {
	const std::string source = readStringFromFile(path);
	std::stringstream input(source);
	std::stringstream output;
	std::string line;

	while (std::getline(input, line)) {
		std::string trimmed = line;
		// simple left-trim
		trimmed.erase(trimmed.begin(), std::find_if(trimmed.begin(), trimmed.end(), [](unsigned char ch) {
			return !std::isspace(ch);
		}));

		if (trimmed.rfind("#include", 0) == 0) {
			std::size_t firstQuote = trimmed.find('\"');
			std::size_t secondQuote = trimmed.find('\"', firstQuote + 1);
			if (firstQuote != std::string::npos && secondQuote != std::string::npos && secondQuote > firstQuote + 1) {
				std::string includeName = trimmed.substr(firstQuote + 1, secondQuote - firstQuote - 1);
				std::filesystem::path includePath = path.parent_path() / includeName;
				output << preprocessShaderSource(includePath) << '\n';
				continue;
			}
		}

		output << line << '\n';
	}

	return output.str();
}

Shader::Shader(const std::filesystem::path &path) {
	type = (path.extension() == ".frag") ? ShaderType::Fragment : ShaderType::Vertex;

	ID = glCreateShader((GLuint)type);
	source = preprocessShaderSource(path);
	const char *temporaryPointer = source.c_str();
	glShaderSource(ID, 1, &temporaryPointer, NULL);
	glCompileShader(ID);

	int compileSuccess;
	char infoLog[512];
	glGetShaderiv(ID, GL_COMPILE_STATUS, &compileSuccess);
	success = compileSuccess;
	if (!success) {
		glGetShaderInfoLog(ID, 512, NULL, infoLog);
		std::cout << "Error while compiling shader" + path.string() + "\n" << infoLog << std::endl;
	}
}
#endif
