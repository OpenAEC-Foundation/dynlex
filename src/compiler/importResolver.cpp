#include "compiler/importResolver.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>

namespace fs = std::filesystem;

namespace tbx {

ImportResolver::ImportResolver(const std::string &baseDirParam)
    : baseDir(baseDirParam) {
  // Normalize base directory
  if (!baseDir.empty() && fs::exists(baseDir)) {
    baseDir = fs::canonical(baseDir).string();
  }
}

std::string ImportResolver::readFile(const std::string &path) {
  std::ifstream file(path);
  if (!file) {
    diagnosticsData.emplace_back("Cannot open file: " + path, path);
    return "";
  }
  std::stringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

std::string ImportResolver::resolveImportPath(const std::string &importPath,
                                              const std::string &sourceFile) {
  fs::path sourceDir = fs::path(sourceFile).parent_path();
  if (sourceDir.empty())
    sourceDir = ".";

  // Normalize the import path - add .3bx extension if missing
  std::string normalizedImport = importPath;
  if (normalizedImport.find(".3bx") == std::string::npos) {
    normalizedImport += ".3bx";
  }

  // Try 1: Relative to source file
  fs::path relativePath = sourceDir / normalizedImport;
  if (fs::exists(relativePath)) {
    return fs::canonical(relativePath).string();
  }

  // Try 2: Check if import starts with "lib/" - resolve relative to source
  if (importPath.substr(0, 4) == "lib/") {
    // Remove "lib/" prefix and try finding in lib directory
    std::string libImport = importPath.substr(4);
    if (libImport.find(".3bx") == std::string::npos) {
      libImport += ".3bx";
    }

    // Search up from source directory
    fs::path searchDir = sourceDir;
    for (int searchDepth = 0; searchDepth < 10; searchDepth++) {
      fs::path libPath = searchDir / "lib" / libImport;
      if (fs::exists(libPath)) {
        return fs::canonical(libPath).string();
      }
      fs::path parent = searchDir.parent_path();
      if (parent == searchDir)
        break;
      searchDir = parent;
    }
  }

  // Try 3: Search for lib/ folder up the directory tree from source
  fs::path searchDir = sourceDir;
  for (int searchDepth = 0; searchDepth < 10; searchDepth++) {
    fs::path libPath = searchDir / "lib" / normalizedImport;
    if (fs::exists(libPath)) {
      return fs::canonical(libPath).string();
    }
    fs::path parent = searchDir.parent_path();
    if (parent == searchDir)
      break;
    searchDir = parent;
  }

  // Try 4: Search from base directory
  if (!baseDir.empty()) {
    fs::path libPath = fs::path(baseDir) / "lib" / normalizedImport;
    if (fs::exists(libPath)) {
      return fs::canonical(libPath).string();
    }
  }

  // Return original path - will fail later with proper error
  diagnosticsData.emplace_back("Cannot resolve import: " + importPath,
                               sourceFile);
  return importPath;
}

std::string ImportResolver::extractImportPath(const std::string &line) {
  // Trim leading whitespace
  size_t start = 0;
  while (start < line.size() && (line[start] == ' ' || line[start] == '\t')) {
    start++;
  }

  // Check if line starts with "import " (simple text matching, not a keyword)
  const std::string importPrefix = "import ";
  if (line.size() >= start + importPrefix.size() &&
      line.substr(start, importPrefix.size()) == importPrefix) {
    std::string path = line.substr(start + importPrefix.size());

    // Trim trailing whitespace
    size_t end = path.size();
    while (end > 0 && (path[end - 1] == ' ' || path[end - 1] == '\t' ||
                       path[end - 1] == '\r' || path[end - 1] == '\n')) {
      end--;
    }
    return path.substr(0, end);
  }

  return "";
}

std::string ImportResolver::processSource(const std::string &source,
                                          const std::string &sourcePath,
                                          std::set<std::string> &visited) {
  // Get canonical path to avoid circular imports
  std::string canonicalPath;
  if (fs::exists(sourcePath)) {
    canonicalPath = fs::canonical(sourcePath).string();
  } else {
    canonicalPath = sourcePath;
  }

  // Check for circular import
  if (visited.count(canonicalPath)) {
    return ""; // Already processed, skip
  }
  visited.insert(canonicalPath);
  resolvedFilesData.push_back(canonicalPath);

  std::stringstream result;
  std::istringstream stream(source);
  std::string line;
  int currentLineNumber = 0;

  while (std::getline(stream, line)) {
    currentLineNumber++;

    // Check if this is an import line
    std::string importPath = extractImportPath(line);

    if (!importPath.empty()) {
      // Resolve the import path
      std::string resolvedPath = resolveImportPath(importPath, sourcePath);

      if (fs::exists(resolvedPath)) {
        // Read and process the imported file
        std::string importedSource = readFile(resolvedPath);
        if (!importedSource.empty()) {
          // Add a comment marker for debugging
          std::string beginMsg = "# Begin import: " + importPath + "\n";
          result << beginMsg;
          currentMergedLine++; // Comment line

          result << processSource(importedSource, resolvedPath, visited);

          std::string endMsg = "# End import: " + importPath + "\n";
          result << endMsg;
          currentMergedLine++; // Comment line
        }
      } else {
        // Keep the import line as a comment with error
        std::string errMsg =
            "# ERROR: Cannot find import: " + importPath + "\n";
        result << errMsg;
        currentMergedLine++;
      }
    } else {
      // Not an import line, keep as-is
      result << line << "\n";
      currentMergedLine++;

      // Record source mapping
      sourceMapData[currentMergedLine] = {sourcePath, currentLineNumber};
    }
  }

  return result.str();
}

std::string ImportResolver::resolve(const std::string &filePath) {
  // Reset state
  sourceMapData.clear();
  currentMergedLine = 0;
  resolvedFilesData.clear();
  diagnosticsData.clear();

  // Read the source file
  std::string source = readFile(filePath);
  if (source.empty() && !diagnosticsData.empty()) {
    return "";
  }

  return resolveSource(source, filePath);
}

std::string
ImportResolver::resolveWithPrelude(const std::string &filePath,
                                   const std::string &overrideContent) {
  // Reset state
  sourceMapData.clear();
  currentMergedLine = 0;
  resolvedFilesData.clear();
  diagnosticsData.clear();

  // Read the source file or use override
  std::string source = overrideContent;
  if (source.empty()) {
    source = readFile(filePath);
  }

  if (source.empty() && !diagnosticsData.empty()) {
    return "";
  }

  // Auto-prepend prelude import if not already present
  if (source.find("import lib/prelude") == std::string::npos &&
      source.find("import prelude") == std::string::npos) {
    // Manually handle the prelude injection in the merged output and source map
    // We can't just modify 'source' because we need to track lines

    std::string resolvedPath = resolveImportPath("lib/prelude.3bx", filePath);

    std::stringstream result;

    if (fs::exists(resolvedPath)) {
      std::string importedSource = readFile(resolvedPath);
      if (!importedSource.empty()) {
        result << "# Begin import: lib/prelude\n";
        currentMergedLine++;

        std::set<std::string>
            visited; // Local visited set for prelude to avoid recursion issues
        result << processSource(importedSource, resolvedPath, visited);

        result << "# End import: lib/prelude\n";
        currentMergedLine++;
      }
    }

    // Now process the main file
    std::set<std::string> visited;
    result << processSource(source, filePath, visited);
    return result.str();
  }

  return resolveSource(source, filePath);
}

std::string ImportResolver::resolveSource(const std::string &source,
                                          const std::string &sourcePath) {
  std::set<std::string> visited;
  return processSource(source, sourcePath, visited);
}

} // namespace tbx
