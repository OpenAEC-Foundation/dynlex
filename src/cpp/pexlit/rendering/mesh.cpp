#ifdef PEXLIT_GL
#include <glm/glm.hpp>
#include <mesh.h>
#include <numbers>
#include <vector>

Mesh makeSquare() {

	glm::vec3 vertices[] = {
		{0, 0, 0}, // bottom left
		{1, 0, 0}, // bottom right
		{1, 1, 0}, // top right
		{0, 1, 0}  // top left
	};
	int indices[] = {// counter clockwise indices
					 0, 1, 2, 0, 2, 3
	};
	// https://learnopengl.com/Getting-started/Hello-Triangle

	return Mesh(vertices, indices);
}

void Mesh::calculateNormals(std::span<glm::vec3> vertices, std::span<int> indices, std::vector<glm::vec3> &outNormals) {
	outNormals.assign(vertices.size(), glm::vec3(0.0f));
	for (size_t i = 0; i + 2 < static_cast<size_t>(indices.size()); i += 3) {
		int ia = indices[i];
		int ib = indices[i + 1];
		int ic = indices[i + 2];
		const glm::vec3 &a = vertices[ia];
		const glm::vec3 &b = vertices[ib];
		const glm::vec3 &c = vertices[ic];
		glm::vec3 edge1 = b - a;
		glm::vec3 edge2 = c - a;
		glm::vec3 n = glm::cross(edge1, edge2);
		outNormals[ia] += n;
		outNormals[ib] += n;
		outNormals[ic] += n;
	}
	for (glm::vec3 &n : outNormals) {
		float len2 = glm::dot(n, n);
		if (len2 > 0.0f) {
			n /= std::sqrt(len2);
		}
	}
}

Mesh::Mesh(std::span<glm::vec3> vertices, std::span<int> indices) {
	indiceCount = indices.size();
	if (!indiceCount)
		// don't generate objects for an empty mesh
		return;
	glGenVertexArrays(1, &vertexArrayObject);
	// ..:: Initialization code (done once (unless your object frequently changes)) :: ..
	// 1. bind Vertex Array Object
	glBindVertexArray(vertexArrayObject);

	glGenBuffers(1, &vertexBufferObject);
	// 2. copy our vertices array in a buffer for OpenGL to use
	glBindBuffer(GL_ARRAY_BUFFER, vertexBufferObject);
	glBufferData(GL_ARRAY_BUFFER, vertices.size_bytes(), vertices.data(), GL_STATIC_DRAW);
	// 3. copy our index array in a element buffer for OpenGL to use
	glGenBuffers(1, &elementBufferObject);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, elementBufferObject);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size_bytes(), indices.data(), GL_STATIC_DRAW);

	// 4. position (3 floats) at location 0
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void *)0);
	glEnableVertexAttribArray(0);

	// 5. optional normals at location 1
	std::vector<glm::vec3> normals;
	calculateNormals(vertices, indices, normals);
	glGenBuffers(1, &normalBufferObject);
	glBindBuffer(GL_ARRAY_BUFFER, normalBufferObject);
	glBufferData(GL_ARRAY_BUFFER, normals.size() * sizeof(glm::vec3), normals.data(), GL_STATIC_DRAW);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void *)0);
	glEnableVertexAttribArray(1);
}

Mesh::Mesh(std::span<glm::vec3> vertices, std::span<int> indices, std::span<glm::vec3> normals) {
	indiceCount = indices.size();
	if (!indiceCount)
		return;

	glGenVertexArrays(1, &vertexArrayObject);
	glBindVertexArray(vertexArrayObject);

	glGenBuffers(1, &vertexBufferObject);
	glBindBuffer(GL_ARRAY_BUFFER, vertexBufferObject);
	glBufferData(GL_ARRAY_BUFFER, vertices.size_bytes(), vertices.data(), GL_STATIC_DRAW);

	glGenBuffers(1, &elementBufferObject);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, elementBufferObject);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size_bytes(), indices.data(), GL_STATIC_DRAW);

	// positions at location 0
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void *)0);
	glEnableVertexAttribArray(0);

	// provided normals at location 1
	std::vector<glm::vec3> normalsCopy;
	normalsCopy.assign(normals.begin(), normals.end());
	glGenBuffers(1, &normalBufferObject);
	glBindBuffer(GL_ARRAY_BUFFER, normalBufferObject);
	glBufferData(GL_ARRAY_BUFFER, normalsCopy.size() * sizeof(glm::vec3), normalsCopy.data(), GL_STATIC_DRAW);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void *)0);
	glEnableVertexAttribArray(1);
}

Mesh::Mesh(Mesh &&other) noexcept
	: vertexArrayObject(other.vertexArrayObject), vertexBufferObject(other.vertexBufferObject),
	  elementBufferObject(other.elementBufferObject), normalBufferObject(other.normalBufferObject) {
	other.vertexArrayObject = 0;
	other.vertexBufferObject = 0;
	other.elementBufferObject = 0;
	other.normalBufferObject = 0;
}

Mesh &Mesh::operator=(Mesh &&other) noexcept {
	if (this != &other) {
		// clean up existing GL objects
		if (elementBufferObject) {
			glDeleteBuffers(1, &elementBufferObject);
		}
		if (vertexBufferObject) {
			glDeleteBuffers(1, &vertexBufferObject);
		}
		if (normalBufferObject) {
			glDeleteBuffers(1, &normalBufferObject);
		}
		if (vertexArrayObject) {
			glDeleteVertexArrays(1, &vertexArrayObject);
		}

		vertexArrayObject = other.vertexArrayObject;
		vertexBufferObject = other.vertexBufferObject;
		elementBufferObject = other.elementBufferObject;
		normalBufferObject = other.normalBufferObject;

		other.vertexArrayObject = 0;
		other.vertexBufferObject = 0;
		other.elementBufferObject = 0;
		other.normalBufferObject = 0;
	}
	return *this;
}

Mesh::~Mesh() {
	if (elementBufferObject) {
		glDeleteBuffers(1, &elementBufferObject);
		elementBufferObject = 0;
	}
	if (normalBufferObject) {
		glDeleteBuffers(1, &normalBufferObject);
		normalBufferObject = 0;
	}
	if (vertexBufferObject) {
		glDeleteBuffers(1, &vertexBufferObject);
		vertexBufferObject = 0;
	}
	if (vertexArrayObject) {
		glDeleteVertexArrays(1, &vertexArrayObject);
		vertexArrayObject = 0;
	}
}

void Mesh::render() {
	if (vertexArrayObject) {
		glBindVertexArray(vertexArrayObject);
		glDrawElements(GL_TRIANGLES, indiceCount, GL_UNSIGNED_INT, 0);
	}
}

Mesh makeCube() {
	// Unit cube centered at origin, extents from -1 to 1
	glm::vec3 vertices[] = {
		// Front face
		{-1, -1, 1},
		{1, -1, 1},
		{1, 1, 1},
		{-1, 1, 1},
		// Back face
		{1, -1, -1},
		{-1, -1, -1},
		{-1, 1, -1},
		{1, 1, -1},
		// Left face
		{-1, -1, -1},
		{-1, -1, 1},
		{-1, 1, 1},
		{-1, 1, -1},
		// Right face
		{1, -1, 1},
		{1, -1, -1},
		{1, 1, -1},
		{1, 1, 1},
		// Top face
		{-1, 1, 1},
		{1, 1, 1},
		{1, 1, -1},
		{-1, 1, -1},
		// Bottom face
		{-1, -1, -1},
		{1, -1, -1},
		{1, -1, 1},
		{-1, -1, 1}
	};
	int indices[] = {
		0,	1,	2,	0,	2,	3,	// front
		4,	5,	6,	4,	6,	7,	// back
		8,	9,	10, 8,	10, 11, // left
		12, 13, 14, 12, 14, 15, // right
		16, 17, 18, 16, 18, 19, // top
		20, 21, 22, 20, 22, 23	// bottom
	};
	return Mesh(vertices, indices);
}

Mesh makeSphere() {
	// simple UV sphere centered at origin with radius 1
	const int stacks = 16;
	const int slices = 32;

	std::vector<glm::vec3> vertices;
	vertices.reserve((stacks + 1) * (slices + 1));
	std::vector<int> indices;
	indices.reserve(stacks * slices * 6);

	const float py = std::numbers::pi_v<float>;

	for (int i = 0; i <= stacks; ++i) {
		float v = static_cast<float>(i) / stacks;
		float phi = v * py; // 0..pi
		float y = std::cos(phi);
		float r = std::sin(phi);
		for (int j = 0; j <= slices; ++j) {
			float u = static_cast<float>(j) / slices;
			float theta = u * (2.0f * py); // 0..2pi
			float x = r * std::cos(theta);
			float z = r * std::sin(theta);
			vertices.emplace_back(x, y, z);
		}
	}

	for (int i = 0; i < stacks; ++i) {
		for (int j = 0; j < slices; ++j) {
			int row1 = i * (slices + 1);
			int row2 = (i + 1) * (slices + 1);
			int a = row1 + j;
			int b = row1 + j + 1;
			int c = row2 + j;
			int d = row2 + j + 1;

			// two triangles per quad, counter-clockwise (outward)
			indices.push_back(a);
			indices.push_back(b);
			indices.push_back(c);

			indices.push_back(b);
			indices.push_back(d);
			indices.push_back(c);
		}
	}

	return Mesh(vertices, indices);
}
#endif
