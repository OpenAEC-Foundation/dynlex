#include "pattern/pattern_registry.hpp"
#include <algorithm>
#include <set>

namespace tbx {

// Reserved words that are literals in pattern syntax
static const std::set<std::string> reservedWords = {
    "set", "to", "if", "then", "else", "while", "loop", "function", "return",
    "is", "the", "a", "an", "and", "or", "not", "pattern", "syntax", "when",
    "parsed", "triggered", "priority", "import", "use", "from", "class",
    "members", "created", "each", "member", "of", "new", "with", "by",
    "multiply", "add", "subtract", "divide", "print"
};

PatternRegistry::PatternRegistry() {
    // Initialize with an empty scope
    scopeStack_.push_back({});
}

void PatternRegistry::registerPattern(std::unique_ptr<Pattern> pattern) {
    pattern->compile();
    Pattern* ptr = pattern.get();
    patterns_.push_back(std::move(pattern));

    // Add to sorted list
    sortedPatterns_.push_back(ptr);
    std::sort(sortedPatterns_.begin(), sortedPatterns_.end(),
        [](Pattern* a, Pattern* b) {
            return a->specificity() > b->specificity();
        });

    // Add to pattern tree
    tree_.insert(ptr);
}

void PatternRegistry::registerFromDef(PatternDef* def) {
    auto pattern = std::make_unique<Pattern>();
    pattern->id = "pattern_" + std::to_string(patterns_.size());
    pattern->elements = def->syntax;
    pattern->definition = def;
    registerPattern(std::move(pattern));
}

void PatternRegistry::registerClass(std::unique_ptr<ClassDef> classDef) {
    ClassDef* ptr = classDef.get();
    classIndex_[classDef->name] = ptr;
    classes_.push_back(std::move(classDef));
}

std::vector<Pattern*> PatternRegistry::getCandidates(const std::string& firstWord) {
    return tree_.getCandidates(firstWord);
}

void PatternRegistry::clear() {
    patterns_.clear();
    sortedPatterns_.clear();
    tree_.clear();
    classes_.clear();
    classIndex_.clear();
    allVariables_.clear();
    scopeStack_.clear();
    scopeStack_.push_back({});
    currentScopeLevel_ = 0;
}

void PatternRegistry::loadPrimitives() {
    // Primitives are defined in the prelude.3bx file
    // This method is a placeholder for any C++-defined patterns
}

void PatternRegistry::rebuildIndex() {
    tree_.clear();
    sortedPatterns_.clear();

    for (auto& pattern : patterns_) {
        Pattern* ptr = pattern.get();
        sortedPatterns_.push_back(ptr);
        tree_.insert(ptr);
    }

    std::sort(sortedPatterns_.begin(), sortedPatterns_.end(),
        [](Pattern* a, Pattern* b) {
            return a->specificity() > b->specificity();
        });
}

void PatternRegistry::pushScope() {
    currentScopeLevel_++;
    scopeStack_.push_back({});
}

void PatternRegistry::popScope() {
    if (currentScopeLevel_ > 0) {
        scopeStack_.pop_back();
        currentScopeLevel_--;
    }
}

void PatternRegistry::defineVariable(const std::string& name) {
    if (!scopeStack_.empty()) {
        scopeStack_.back().insert(name);
    }
    allVariables_.insert(name);
}

bool PatternRegistry::isVariableDefined(const std::string& name) const {
    // Check from innermost to outermost scope
    for (auto it = scopeStack_.rbegin(); it != scopeStack_.rend(); ++it) {
        if (it->count(name)) {
            return true;
        }
    }
    return false;
}

ClassDef* PatternRegistry::getClass(const std::string& name) {
    auto it = classIndex_.find(name);
    if (it != classIndex_.end()) {
        return it->second;
    }
    return nullptr;
}

bool PatternRegistry::isReservedWord(const std::string& word) {
    return reservedWords.count(word) > 0;
}

} // namespace tbx
