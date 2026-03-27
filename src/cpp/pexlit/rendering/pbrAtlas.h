#pragma once
#ifdef PEXLIT_GL
#include "pexlit/safeInclude/includeGL.h"
#include <filesystem>
#include <string>
#include <unordered_map>

struct Texture;

// PBR texture atlas for triplanar mapping
// Packs multiple materials into atlas textures:
// - Material atlas: Grayscale (R) + Roughness (G) + AO (B) + Height (A)
// - Normal atlas: Normal RGB
struct PBRAtlas {
	static constexpr int atlasSize = 1024; // Each texture is 1K
	static constexpr int maxMaterials = 8; // Max materials in atlas

	// Atlas textures (2D array textures for efficient sampling)
	GLuint materialArray{}; // RGBA: grayscale, roughness, AO, height
	GLuint normalArray{};	// RGB: normal map

	// Material name to array index mapping
	std::unordered_map<std::string, int> materialIndices;
	int materialCount{};

	PBRAtlas() = default;
	~PBRAtlas();

	// Cleanup OpenGL resources (call before context destruction)
	void cleanup();

	// Initialize atlas, loading from cache or packing from source
	void init(const std::filesystem::path &sourceDir, const std::filesystem::path &cacheDir);

	// Get texture array index for a material name (returns 0/grass if not found)
	int getMaterialIndex(const std::string &name) const;

	// Bind textures for rendering
	void bind(int materialUnit, int normalUnit) const;

  private:
	void packTextures(const std::filesystem::path &sourceDir, const std::filesystem::path &cacheDir);
	bool loadFromCache(const std::filesystem::path &cacheDir);
	void createArrayTextures();
};

// Global atlas instance
PBRAtlas &getPBRAtlas();
#endif
