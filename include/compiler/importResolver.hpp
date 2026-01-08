#pragma once

#include "compiler/diagnostic.hpp"
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace tbx {

/**
 * ImportResolver - Step 1 of the 3BX compiler pipeline
 *
 * Reads source files and merges them by resolving imports.
 * Import lines are replaced with the contents of the imported files.
 *
 * Key principle: NO hardcoded keywords. The word "import" is detected
 * by simple text matching at the start of a line, not as a reserved keyword.
 */
class ImportResolver {
public:
  /**
   * Construct an ImportResolver with the base directory for lib/ resolution
   * @param baseDir The directory to search for lib/ folder
   */
  explicit ImportResolver(const std::string &baseDir = ".");

  /**
   * Resolve all imports in a source file and return merged source
   * @param filePath Path to the source file
   * @return Merged source code with all imports inlined
   */
  std::string resolve(const std::string &filePath);

  /**
   * Resolve all imports in a source file, auto-loading prelude if not imported
   * @param filePath Path to the source file
   * @param overrideContent Optional content to use instead of reading from
   * filePath
   * @return Merged source code with all imports inlined (including prelude)
   */
  std::string resolveWithPrelude(const std::string &filePath,
                                 const std::string &overrideContent = "");

  /**
   * Resolve imports from source code string
   * @param source The source code string
   * @param sourcePath Path of the source file (for relative imports)
   * @return Merged source code with all imports inlined
   */
  std::string resolveSource(const std::string &source,
                            const std::string &sourcePath);

  /**
   * Get the list of resolved file paths (for debugging)
   */
  const std::vector<std::string> &resolvedFiles() const {
    return resolvedFilesData;
  }

  /**
   * Get any errors that occurred during resolution
   */
  const std::vector<Diagnostic> &diagnostics() const { return diagnosticsData; }

  /**
   * Source mapping structures
   */
  struct SourceLocation {
    std::string filePath;
    int lineNumber;
  };

  /**
   * Get the source map (merged line -> original location)
   */
  const std::map<int, SourceLocation> &sourceMap() const {
    return sourceMapData;
  }

private:
  /**
   * Read file contents
   */
  std::string readFile(const std::string &path);

  /**
   * Resolve an import path to an actual file path
   * Searches: relative to source, lib/ directory, up the tree
   */
  std::string resolveImportPath(const std::string &importPath,
                                const std::string &sourceFile);

  /**
   * Process a single source, resolving imports recursively
   */
  std::string processSource(const std::string &source,
                            const std::string &sourcePath,
                            std::set<std::string> &visited);

  /**
   * Check if a line is an import line and extract the import path
   * Returns empty string if not an import line
   */
  std::string extractImportPath(const std::string &line);

  std::string baseDir;
  std::vector<std::string> resolvedFilesData;
  std::vector<Diagnostic> diagnosticsData;
  std::map<int, SourceLocation> sourceMapData;
  int currentMergedLine = 0;
};

} // namespace tbx
