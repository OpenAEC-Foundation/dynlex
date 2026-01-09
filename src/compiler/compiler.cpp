#include "compiler.h"
#include <regex>
#include "fileFunctions.h"
#include "stringFunctions.h"
#include "sourceFile.h"

// non-capturing regex for line terminators
const std::regex lineTerminatorRegex("/(?<=\r\n|\r(?!\n)|\n)/g");

bool importSourceFile(const std::string &path, ParseContext &context)
{
	std::string text;

	if (!readStringFromFile(path, text))
	{
		return false;
	}

	// split on any line ending and capture the ending too
	std::vector<std::string> lines = splitString(text, lineTerminatorRegex);

	SourceFile *sourceFile = new SourceFile(path, text);

	for (const std::string &lineString : lines)
	{
		CodeLine line = CodeLine(lineString, sourceFile);
		// first, remove comments and trim whitespace from the right

		line.rightTrimmedText = std::regex_replace(lineString, std::regex("#.*$"), "");

		// check if the line is an import statement
		if (line.rightTrimmedText.starts_with("import "))
		{
			// if so, recursively import the file
			// extract the file path
			std::string importPath = line.rightTrimmedText.substr(std::string_view("import ").length());
			if (!importSourceFile(importPath, context))
			{
				return false;
			}

		}
		context.codeLines.push_back(line);
	}
	return true;
}

//step 2: analyze sections
bool analyzeSections(ParseContext &context)
{
	//

	return true;
}