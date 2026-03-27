#pragma once
#ifdef PEXLIT_GL
#include "safeInclude/includeGL.h"
#include <glm/vec3.hpp>
#include <span>
#include <vector>
struct Mesh {
	// just an array of floats
	GLuint vertexArrayObject{};
	// a buffer, 'explaining' the float array
	GLuint vertexBufferObject{};
	// an index buffer
	GLuint elementBufferObject{};
	// optional normals buffer (layout location 1)
	GLuint normalBufferObject{};
	GLsizei indiceCount{};
	Mesh() = default;
	Mesh(std::span<glm::vec3> vertices, std::span<int> indices);
	Mesh(std::span<glm::vec3> vertices, std::span<int> indices, std::span<glm::vec3> normals);
	Mesh(const Mesh &) = delete;
	Mesh &operator=(const Mesh &) = delete;
	Mesh(Mesh &&other) noexcept;
	Mesh &operator=(Mesh &&other) noexcept;
	~Mesh();
	inline explicit operator bool() const { return vertexArrayObject != 0; }
	void render();

	static void calculateNormals(std::span<glm::vec3> vertices, std::span<int> indices, std::vector<glm::vec3> &outNormals);
};

Mesh makeSquare();
Mesh makeSphere();
Mesh makeCube();
#endif
