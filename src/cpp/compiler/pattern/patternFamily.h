#pragma once
#include <unordered_set>

// Definitions connected through shared trie endpoints have one syntax family,
// including connections through a definition's choice alternatives.
struct PatternFamily {
	// Precedence is a partial order. Sharing a predecessor does not order peers.
	std::unordered_set<const PatternFamily *> precedenceSuccessors;
};
