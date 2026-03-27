#ifdef PEXLIT_GL
#include "pbrAtlas.h"
#include "stb_image.h"
#include "stb_image_write.h"
#include <cmath>
#include <iostream>
#include <vector>

static PBRAtlas globalAtlas;

PBRAtlas &getPBRAtlas() { return globalAtlas; }

PBRAtlas::~PBRAtlas() {
	// Resources should be cleaned up via cleanup() before OpenGL context destruction
	// If not, we can't safely call glDeleteTextures here
}

void PBRAtlas::cleanup() {
	if (materialArray) {
		glDeleteTextures(1, &materialArray);
		materialArray = 0;
	}
	if (normalArray) {
		glDeleteTextures(1, &normalArray);
		normalArray = 0;
	}
}

void PBRAtlas::init(const std::filesystem::path &sourceDir, const std::filesystem::path &cacheDir) {
	// Try to load from cache first
	if (loadFromCache(cacheDir)) {
		std::cout << "Loaded PBR atlas from cache\n";
		return;
	}

	// Otherwise pack from source
	std::cout << "Packing PBR textures...\n";
	packTextures(sourceDir, cacheDir);
}

int PBRAtlas::getMaterialIndex(const std::string &name) const {
	auto it = materialIndices.find(name);
	if (it != materialIndices.end())
		return it->second;
	return 0; // Default to first material (grass)
}

void PBRAtlas::bind(int materialUnit, int normalUnit) const {
	glActiveTexture(GL_TEXTURE0 + materialUnit);
	glBindTexture(GL_TEXTURE_2D_ARRAY, materialArray);
	glActiveTexture(GL_TEXTURE0 + normalUnit);
	glBindTexture(GL_TEXTURE_2D_ARRAY, normalArray);
}

bool PBRAtlas::loadFromCache(const std::filesystem::path &cacheDir) {
	std::filesystem::path materialPath = cacheDir / "material_atlas.png";
	std::filesystem::path normalPath = cacheDir / "normal_atlas.png";

	if (!std::filesystem::exists(materialPath) || !std::filesystem::exists(normalPath))
		return false;

	// Load material atlas (RGBA, stacked vertically: 3 materials * 1024 = 3072 height)
	int w, h, channels;
	stbi_set_flip_vertically_on_load(false);
	unsigned char *materialData = stbi_load(materialPath.string().c_str(), &w, &h, &channels, 4);
	if (!materialData || w != atlasSize) {
		if (materialData)
			stbi_image_free(materialData);
		return false;
	}

	materialCount = h / atlasSize;

	// Load normal atlas
	unsigned char *normalData = stbi_load(normalPath.string().c_str(), &w, &h, &channels, 3);
	if (!normalData || w != atlasSize || h != materialCount * atlasSize) {
		stbi_image_free(materialData);
		if (normalData)
			stbi_image_free(normalData);
		return false;
	}

	// Create array textures
	glGenTextures(1, &materialArray);
	glBindTexture(GL_TEXTURE_2D_ARRAY, materialArray);
	glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, atlasSize, atlasSize, materialCount, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

	for (int i = 0; i < materialCount; ++i) {
		glTexSubImage3D(
			GL_TEXTURE_2D_ARRAY, 0, 0, 0, i, atlasSize, atlasSize, 1, GL_RGBA, GL_UNSIGNED_BYTE,
			materialData + i * atlasSize * atlasSize * 4
		);
	}
	glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);

	glGenTextures(1, &normalArray);
	glBindTexture(GL_TEXTURE_2D_ARRAY, normalArray);
	glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGB8, atlasSize, atlasSize, materialCount, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);

	for (int i = 0; i < materialCount; ++i) {
		glTexSubImage3D(
			GL_TEXTURE_2D_ARRAY, 0, 0, 0, i, atlasSize, atlasSize, 1, GL_RGB, GL_UNSIGNED_BYTE,
			normalData + i * atlasSize * atlasSize * 3
		);
	}
	glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);

	stbi_image_free(materialData);
	stbi_image_free(normalData);

	// Set up material indices (order must match packing order)
	materialIndices["grass"] = 0;
	materialIndices["ground"] = 1;
	materialIndices["gravel"] = 2;

	return true;
}

void PBRAtlas::packTextures(const std::filesystem::path &sourceDir, const std::filesystem::path &cacheDir) {
	// Material names and their source folders
	std::vector<std::pair<std::string, std::string>> materials = {
		{"grass", "grass"}, {"ground", "ground"}, {"gravel", "gravel"}
	};

	materialCount = (int)materials.size();

	// Allocate combined data for cache saving
	std::vector<unsigned char> materialAtlasData(atlasSize * atlasSize * 4 * materialCount);
	std::vector<unsigned char> normalAtlasData(atlasSize * atlasSize * 3 * materialCount);

	for (int matIdx = 0; matIdx < materialCount; ++matIdx) {
		const auto &[name, folder] = materials[matIdx];
		materialIndices[name] = matIdx;

		std::filesystem::path matDir = sourceDir / folder;

		// Find texture files (they have prefixes like Grass005_1K-PNG_)
		std::string colorFile, roughnessFile, aoFile, displacementFile, normalFile;
		for (const auto &entry : std::filesystem::directory_iterator(matDir)) {
			std::string filename = entry.path().filename().string();
			if (filename.find("_Color.png") != std::string::npos)
				colorFile = entry.path().string();
			else if (filename.find("_Roughness.png") != std::string::npos)
				roughnessFile = entry.path().string();
			else if (filename.find("_AmbientOcclusion.png") != std::string::npos)
				aoFile = entry.path().string();
			else if (filename.find("_Displacement.png") != std::string::npos)
				displacementFile = entry.path().string();
			else if (filename.find("_NormalGL.png") != std::string::npos)
				normalFile = entry.path().string();
		}

		// Load textures
		int w, h, c;
		stbi_set_flip_vertically_on_load(false);

		unsigned char *colorData = stbi_load(colorFile.c_str(), &w, &h, &c, 3);
		unsigned char *roughnessData = stbi_load(roughnessFile.c_str(), &w, &h, &c, 1);
		unsigned char *aoData = stbi_load(aoFile.c_str(), &w, &h, &c, 1);
		unsigned char *displacementData = stbi_load(displacementFile.c_str(), &w, &h, &c, 1);
		unsigned char *normalData = stbi_load(normalFile.c_str(), &w, &h, &c, 3);

		if (!colorData || !roughnessData || !aoData || !displacementData || !normalData) {
			std::cerr << "Failed to load textures for material: " << name << "\n";
			if (colorData)
				stbi_image_free(colorData);
			if (roughnessData)
				stbi_image_free(roughnessData);
			if (aoData)
				stbi_image_free(aoData);
			if (displacementData)
				stbi_image_free(displacementData);
			if (normalData)
				stbi_image_free(normalData);
			continue;
		}

		// Pack into atlas: Grayscale (R) + Roughness (G) + AO (B) + Height (A)
		unsigned char *matDest = materialAtlasData.data() + matIdx * atlasSize * atlasSize * 4;
		unsigned char *normDest = normalAtlasData.data() + matIdx * atlasSize * atlasSize * 3;

		for (int i = 0; i < atlasSize * atlasSize; ++i) {
			// Convert color to grayscale (luminance)
			float r = colorData[i * 3 + 0] / 255.0f;
			float g = colorData[i * 3 + 1] / 255.0f;
			float b = colorData[i * 3 + 2] / 255.0f;
			float grayscale = 0.2126f * r + 0.7152f * g + 0.0722f * b;

			matDest[i * 4 + 0] = (unsigned char)(grayscale * 255.0f);
			matDest[i * 4 + 1] = roughnessData[i];
			matDest[i * 4 + 2] = aoData[i];
			matDest[i * 4 + 3] = displacementData[i];

			normDest[i * 3 + 0] = normalData[i * 3 + 0];
			normDest[i * 3 + 1] = normalData[i * 3 + 1];
			normDest[i * 3 + 2] = normalData[i * 3 + 2];
		}

		stbi_image_free(colorData);
		stbi_image_free(roughnessData);
		stbi_image_free(aoData);
		stbi_image_free(displacementData);
		stbi_image_free(normalData);

		std::cout << "Packed material: " << name << "\n";
	}

	// Create GL textures
	glGenTextures(1, &materialArray);
	glBindTexture(GL_TEXTURE_2D_ARRAY, materialArray);
	glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, atlasSize, atlasSize, materialCount, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

	for (int i = 0; i < materialCount; ++i) {
		glTexSubImage3D(
			GL_TEXTURE_2D_ARRAY, 0, 0, 0, i, atlasSize, atlasSize, 1, GL_RGBA, GL_UNSIGNED_BYTE,
			materialAtlasData.data() + i * atlasSize * atlasSize * 4
		);
	}
	glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);

	glGenTextures(1, &normalArray);
	glBindTexture(GL_TEXTURE_2D_ARRAY, normalArray);
	glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGB8, atlasSize, atlasSize, materialCount, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);

	for (int i = 0; i < materialCount; ++i) {
		glTexSubImage3D(
			GL_TEXTURE_2D_ARRAY, 0, 0, 0, i, atlasSize, atlasSize, 1, GL_RGB, GL_UNSIGNED_BYTE,
			normalAtlasData.data() + i * atlasSize * atlasSize * 3
		);
	}
	glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);

	// Save to cache
	std::filesystem::create_directories(cacheDir);

	// Save as vertically stacked image (each layer below the previous)
	stbi_write_png(
		(cacheDir / "material_atlas.png").string().c_str(), atlasSize, atlasSize * materialCount, 4, materialAtlasData.data(),
		atlasSize * 4
	);
	stbi_write_png(
		(cacheDir / "normal_atlas.png").string().c_str(), atlasSize, atlasSize * materialCount, 3, normalAtlasData.data(),
		atlasSize * 3
	);

	std::cout << "Saved PBR atlas to cache\n";
}
#endif
