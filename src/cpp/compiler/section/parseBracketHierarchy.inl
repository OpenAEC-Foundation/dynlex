StringHierarchy *parseBracketHierarchy(ParseContext &context, Range range) {
	std::stack<StringHierarchy *> nodeStack;
	StringHierarchy *base = new StringHierarchy(0, 0);
	nodeStack.push(base);

	for (size_t index = 0; index < range.subString.size(); index++) {
		char character = range.subString[index];
		if (character == argumentChar)
			crashCompilerBug("internal argument placeholder reached bracket parsing");

		auto push = [&nodeStack, index, character] {
			StringHierarchy *newChild = new StringHierarchy(character, index + 1);
			nodeStack.top()->children.push_back(newChild);
			nodeStack.push(newChild);
		};
		auto tryPop = [&nodeStack, &context, &range, base, index, character](char requiredCharacter) {
			if (nodeStack.top()->character == requiredCharacter) {
				nodeStack.top()->end = index;
				nodeStack.pop();
				return true;
			} else {
				delete base;
				context.diagnostics.push_back(Diagnostic(
					context, Diagnostic::Level::Error, "unmatched closing character",
					Range(range.line, range.subString.substr(index, 1)), "character", std::string(1, character)
				));
				return false;
			}
		};

		switch (character) {
		case '(': {
			push();
			break;
		}
		case '[': {
			push();
			break;
		}
		case '{': {
			push();
			break;
		}
		case ')': {
			if (nodeStack.top()->character == ',') {
				nodeStack.top()->end = index;
				nodeStack.pop();
			}
			if (!tryPop('('))
				return nullptr;
			break;
		}
		case ']': {
			if (nodeStack.top()->character == ',') {
				nodeStack.top()->end = index;
				nodeStack.pop();
			}
			if (!tryPop('['))
				return nullptr;
			break;
		}
		case '}': {
			if (!tryPop('{'))
				return nullptr;
			break;
		}
		case '"': {
			push();
			auto stringIt = range.subString.begin() + index;
			while (true) {
				stringIt = std::find(stringIt + 1, range.subString.end(), '\"');
				if (stringIt == range.subString.end()) {
					context.diagnostics.push_back(Diagnostic(
						context, Diagnostic::Level::Error, "unmatched string character",
						Range(range.line, range.subString.substr(index, 1))
					));
					delete base;
					return nullptr;
				}
				if (!isEscapedCharacter(range.subString, stringIt - range.subString.begin())) {
					index = stringIt - range.subString.begin();
					break;
				}
			};
			nodeStack.top()->end = index;
			nodeStack.pop();
			break;
		}
		case '\\': {
			if (nodeStack.top()->character == '"')
				// skip the next character
				index++;
			break;
		}
		case ',': {
			// Commas separate arguments only in @intrinsic argument lists and
			// array literals. In grouping parentheses they stay ordinary text,
			// so patterns containing a literal comma (like the select pattern
			// "$ if $, else $") still match inside them.
			auto isIntrinsicArgumentParen = [&range](const StringHierarchy *parenNode) {
				constexpr std::string_view intrinsicKeyword = "@intrinsic"sv;
				if (parenNode->start < 1)
					return false;
				size_t parenPos = static_cast<size_t>(parenNode->start) - 1;
				return parenPos >= intrinsicKeyword.length() &&
					   range.subString.substr(parenPos - intrinsicKeyword.length(), intrinsicKeyword.length()) ==
						   intrinsicKeyword;
			};
			if (nodeStack.top()->character == '[' ||
				(nodeStack.top()->character == '(' && isIntrinsicArgumentParen(nodeStack.top()))) {
				// add the child, don't push
				StringHierarchy *newChild = new StringHierarchy(character, nodeStack.top()->start);
				// move all other children to this new child
				newChild->children = nodeStack.top()->children;
				newChild->end = index;
				nodeStack.top()->children = {newChild};
				// add another ',' child and push
				push();
			} else if (nodeStack.top()->character == ',') {
				nodeStack.top()->end = index;
				nodeStack.pop();
				push();
			} else {
				// Grouping-paren or top-level comma — treat as regular text
			}
			break;
		}
		default:
			break;
		}
	}
	if (nodeStack.size() > 1) {
		while (nodeStack.size() > 1) {
			context.diagnostics.push_back(Diagnostic(
				context, Diagnostic::Level::Error, "unmatched closing character",
				range.subRange(nodeStack.top()->start, nodeStack.top()->start + 1), "character",
				std::string(1, nodeStack.top()->character)
			));
			nodeStack.pop();
		}
		delete base;
		return nullptr;
	}

	base->end = range.end();
	return base;
}
