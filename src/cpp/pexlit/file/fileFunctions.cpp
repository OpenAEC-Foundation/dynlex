#include "fileFunctions.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
bool readStringFromFile(const std::filesystem::path &filePath, std::string &content) {
	const std::ifstream inputStream(filePath, std::ios_base::binary);
	if (inputStream.fail()) {
		return false;
	}

	std::stringstream buffer;
	buffer << inputStream.rdbuf();

	content = buffer.str();
	return true;
}
