#pragma once

#include "pattern/pattern.hpp"
#include "pattern/patternTree.hpp"
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <string>

namespace tbx {

// Tracks variable information for scope management
struct VariableInfo {
    std::string name;
    bool isDefined = false;
    size_t scopeLevel = 0;
};

// Represents a class definition parsed from 3BX code
struct ClassDef {
    std::string name;                              // Class name (from pattern)
    std::vector<PatternElement> patternSyntax;     // Pattern to match for creation
    std::vector<std::string> members;              // Member variable names
    std::vector<StmtPtr> constructorBody;          // When created: body
    std::vector<std::pair<std::string, std::vector<StmtPtr>>> methods; // Method definitions
};

class PatternRegistry {
public:
    PatternRegistry();

    // Register a new pattern
    void registerPattern(std::unique_ptr<Pattern> pattern);

    // Register a pattern from a PatternDef AST node
    void registerFromDef(PatternDef* def);

    // Register a class definition
    void registerClass(std::unique_ptr<ClassDef> classDef);

    // Get all patterns that could match starting with given word
    std::vector<Pattern*> getCandidates(const std::string& firstWord);

    // Get all registered patterns
    const std::vector<Pattern*>& allPatterns() const { return sortedPatterns_; }

    // Get pattern tree for traversal matching
    PatternTree& getTree() { return tree_; }

    // Clear all patterns
    void clear();

    // Load built-in/primitive patterns
    void loadPrimitives();

    // Variable scope management
    void pushScope();
    void popScope();
    void defineVariable(const std::string& name);
    bool isVariableDefined(const std::string& name) const;
    const std::unordered_set<std::string>& getVariables() const { return allVariables_; }

    // Class lookup
    ClassDef* getClass(const std::string& name);
    const std::vector<std::unique_ptr<ClassDef>>& allClasses() const { return classes_; }

    // Check if a word is a reserved keyword
    static bool isReservedWord(const std::string& word);

private:
    std::vector<std::unique_ptr<Pattern>> patterns_;
    std::vector<Pattern*> sortedPatterns_;  // Sorted by priority
    PatternTree tree_;

    // Class definitions
    std::vector<std::unique_ptr<ClassDef>> classes_;
    std::unordered_map<std::string, ClassDef*> classIndex_;

    // Variable tracking
    std::vector<std::unordered_set<std::string>> scopeStack_;
    std::unordered_set<std::string> allVariables_;
    size_t currentScopeLevel_ = 0;

    void rebuildIndex();
};

} // namespace tbx
