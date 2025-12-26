#include "pattern/patternTree.hpp"
#include <algorithm>

namespace tbx {

PatternTree::PatternTree() : root_(std::make_unique<PatternTreeNode>()) {}

void PatternTree::insert(Pattern* pattern) {
    if (!pattern || pattern->elements.empty()) {
        return;
    }

    // Check if pattern starts with a parameter
    bool startsWithParam = !pattern->elements.empty() && pattern->elements[0].is_param;

    // Update index for quick lookup by first word
    // For optional first elements, we need special handling
    if (pattern->first_word.empty()) {
        paramFirstPatterns_.push_back(pattern);
    } else {
        wordIndex_[pattern->first_word].push_back(pattern);

        // If pattern starts with a param (like "<var>'s ..."), also add to paramFirstPatterns
        // so it can be found when matching expressions like "5.5's ..."
        if (startsWithParam) {
            paramFirstPatterns_.push_back(pattern);
        }
    }

    // Also index by first required word for patterns starting with optional elements
    // This allows "set result to 5" to match pattern "set [the] result to value"
    std::string firstRequiredWord;
    for (const auto& elem : pattern->elements) {
        if (!elem.is_param && !elem.is_optional) {
            firstRequiredWord = elem.value;
            break;
        }
    }
    if (!firstRequiredWord.empty() && firstRequiredWord != pattern->first_word) {
        wordIndex_[firstRequiredWord].push_back(pattern);
    }

    // Insert into trie structure with support for optional elements
    PatternTreeNode* current = root_.get();

    for (size_t elemIndex = 0; elemIndex < pattern->elements.size(); ++elemIndex) {
        const auto& elem = pattern->elements[elemIndex];

        if (!elem.is_param) {
            // Literal word - follow or create literal child
            if (!current->literalChildren.count(elem.value)) {
                current->literalChildren[elem.value] = std::make_unique<PatternTreeNode>();
            }

            // If this literal is optional, mark it as such
            if (elem.is_optional) {
                current->optionalLiterals.insert(elem.value);
            }

            current = current->literalChildren[elem.value].get();
        } else {
            // Parameter - follow or create parameter child
            if (!current->paramChild) {
                current->paramChild = std::make_unique<PatternTreeNode>();
            }
            current = current->paramChild.get();
            current->paramName = elem.value;
        }
    }

    // Mark this node as ending a pattern
    current->endingPatterns.push_back(pattern);

    // Sort ending patterns by specificity (most specific first)
    std::sort(current->endingPatterns.begin(), current->endingPatterns.end(),
        [](Pattern* a, Pattern* b) {
            return a->specificity() > b->specificity();
        });
}

void PatternTree::clear() {
    root_ = std::make_unique<PatternTreeNode>();
    paramFirstPatterns_.clear();
    wordIndex_.clear();
}

std::vector<Pattern*> PatternTree::getCandidates(const std::string& firstWord) {
    std::vector<Pattern*> result;

    // Add patterns that start with this word
    auto it = wordIndex_.find(firstWord);
    if (it != wordIndex_.end()) {
        result.insert(result.end(), it->second.begin(), it->second.end());
    }

    // Add patterns that start with a parameter (always candidates)
    result.insert(result.end(), paramFirstPatterns_.begin(), paramFirstPatterns_.end());

    // Sort by specificity (more literals = more specific = try first)
    std::sort(result.begin(), result.end(),
        [](Pattern* a, Pattern* b) {
            return a->specificity() > b->specificity();
        });

    return result;
}

} // namespace tbx
