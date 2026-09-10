#pragma once
#include "lspProtocol.h"
#include "sourceFile.h"
#include "textEncoding.h"

namespace lsp {

inline Position editorPosition(const SourceFile &source, Position position) {
	return {position.line, utf16Column(source.getLine(position.line), position.character)};
}

inline Position compilerPosition(const SourceFile &source, Position position) {
	return {position.line, byteColumn(source.getLine(position.line), position.character)};
}

inline Range editorRange(const SourceFile &source, Range range) {
	return {editorPosition(source, range.start), editorPosition(source, range.end)};
}

inline CompletionList editorCompletions(const SourceFile &source, CompletionList result) {
	for (auto &item : result.items) {
		if (item.textEdit)
			item.textEdit->range = editorRange(source, item.textEdit->range);
	}
	return result;
}

} // namespace lsp
