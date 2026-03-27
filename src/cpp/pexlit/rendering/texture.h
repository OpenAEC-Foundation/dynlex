#pragma once
#ifdef PEXLIT_GL
#include "safeInclude/includeGL.h"
#include <filesystem>
#include <vector>
struct Texture {
	uint8_t *data{};
	GLuint width{}, height{}, ID{};

	Texture(GLuint width, GLuint height, uint8_t *data = nullptr);
	Texture(
		GLuint width, GLuint height, const uint8_t *data, GLenum internalFormat, GLenum format, GLenum type,
		bool generateMipmaps
	);
	Texture(const std::filesystem::path &path);
	~Texture();
	void setPixel(GLuint x, GLuint y, uint32_t color);
	void setFiltering(GLenum minFilter, GLenum magFilter);

  private:
	void toGL();
};
#endif
