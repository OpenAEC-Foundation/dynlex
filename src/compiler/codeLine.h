#include <string>
struct SourceFile;
struct CodeLine
{
	CodeLine(std::string text, SourceFile* sourceFile):fullText(fullText), sourceFile(sourceFile) {}
	SourceFile* sourceFile;
	//full text including line terminators
	std::string fullText;
	//the text without comments and right-trimmed
	std::string rightTrimmedText;

};