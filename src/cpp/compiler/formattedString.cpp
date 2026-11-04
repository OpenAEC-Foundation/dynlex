#include "sourceLineBuilder.h"
#include "sourceSyntax.h"
#include "sourceTransform.h"
#include "syntaxConfig.h"
#include <optional>

namespace {
using Span = std::pair<size_t, size_t>;

struct FormatPart {
	bool literal;
	std::vector<Span> spans;
};

class FormattedStringExpansion {
  public:
	FormattedStringExpansion(ParseContext &context, CodeLine &source)
		: context(context), source(source), syntax(syntaxConfigForSourceFile(context, source.sourceFile)),
		  text(source.fullText), builder(context, source) {}

	CodeLine *expand() {
		if (!appendRange(0, text.size()))
			return nullptr;
		return changed ? builder.finish() : &source;
	}

  private:
	ParseContext &context;
	CodeLine &source;
	const SyntaxConfig &syntax;
	std::string_view text;
	LogicalLineBuilder builder;
	bool changed = false;

	bool fail(size_t position, std::string_view detail) {
		context.addDiagnostic(Diagnostic(
			context, Diagnostic::Level::Error, "invalid formatted string", Range(&source, position, position + 1), "detail",
			detail
		));
		return false;
	}

	std::optional<Span> prefixBefore(size_t quote, size_t start) const {
		size_t end = quote;
		while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])))
			end--;
		const std::string &prefix = syntax.formattedStringPrefix;
		if (end < start + prefix.size() || text.substr(end - prefix.size(), prefix.size()) != prefix)
			return std::nullopt;
		size_t begin = end - prefix.size();
		if (begin > start && sourceWordCharacter(text[begin - 1]))
			return std::nullopt;
		return Span{begin, end};
	}

	bool appendRange(size_t start, size_t end) {
		size_t copiedThrough = start;
		for (size_t index = start; index < end; index++) {
			if (!syntax.commentPrefix.empty() && text.substr(index).starts_with(syntax.commentPrefix))
				break;
			if (text[index] != '"')
				continue;
			std::optional<Span> prefix = prefixBefore(index, copiedThrough);
			if (!prefix) {
				size_t quoteEnd = sourceStringEnd(text, index, end);
				if (quoteEnd == std::string_view::npos)
					break;
				index = quoteEnd;
				continue;
			}
			builder.appendSource(copiedThrough, prefix->first);
			if (!appendFormatted(*prefix, index, end))
				return false;
			copiedThrough = index + 1;
			changed = true;
		}
		builder.appendSource(copiedThrough, end);
		return true;
	}

	bool appendFormatted(Span prefix, size_t &quote, size_t end) {
		const size_t openingQuote = quote;
		std::vector<FormatPart> parts;
		FormatPart literal{true, {}};
		size_t literalStart = quote + 1;
		size_t literalTokenStart = openingQuote;
		auto finishLiteral = [&](size_t stop) {
			if (literalStart < stop)
				literal.spans.push_back({literalStart, stop});
			if (!literal.spans.empty())
				parts.push_back(std::move(literal));
			literal = {true, {}};
		};
		bool closed = false;
		for (quote++; quote < end; quote++) {
			char character = text[quote];
			if (isEscapedCharacter(text, quote))
				continue;
			if (character == '"') {
				finishLiteral(quote);
				closed = true;
				break;
			}
			if (character != '{' && character != '}')
				continue;
			if (quote + 1 < end && text[quote + 1] == character) {
				literal.spans.push_back({literalStart, quote + 1});
				quote++;
				literalStart = quote + 1;
				continue;
			}
			if (character == '}')
				return fail(quote, "use '}}' for a literal closing brace");
			context.addSourceToken(Range(&source, literalTokenStart, quote + 1), ParseContext::SourceTokenKind::String);
			finishLiteral(quote);
			size_t expressionStart = quote + 1;
			int depth = 1;
			while (++quote < end && depth > 0) {
				if (text[quote] == '"') {
					size_t stringEnd = sourceStringEnd(text, quote, end);
					if (stringEnd == std::string_view::npos)
						return fail(expressionStart - 1, "unterminated expression");
					quote = stringEnd;
				} else if (text[quote] == '{') {
					depth++;
				} else if (text[quote] == '}') {
					depth--;
				}
			}
			if (depth != 0)
				return fail(expressionStart - 1, "unterminated expression");
			quote--;
			Span expression = trimmedSourceSpan(text, expressionStart, quote);
			if (expression.first == expression.second)
				return fail(expressionStart - 1, "an expression is required between braces");
			parts.push_back({false, {expression}});
			literalStart = quote + 1;
			literalTokenStart = quote;
		}
		if (!closed)
			return fail(openingQuote, "unterminated string");
		context.addSourceToken(Range(&source, literalTokenStart, quote + 1), ParseContext::SourceTokenKind::String);
		if (parts.empty())
			parts.push_back({true, {}});
		builder.appendSynthetic("(", prefix.first);
		builder.appendSource(prefix.first, prefix.second);
		for (size_t index = 0; index < parts.size(); index++) {
			builder.appendSynthetic(
				index == 0					? " ("
				: index + 1 == parts.size() ? " " + syntax.argumentPackConjunction + " ("
											: syntax.argumentPackSeparator + " (",
				openingQuote
			);
			const FormatPart &part = parts[index];
			if (part.literal)
				builder.appendSource(openingQuote, openingQuote + 1);
			for (Span span : part.spans) {
				if (part.literal)
					builder.appendSource(span.first, span.second);
				else if (!appendRange(span.first, span.second))
					return false;
			}
			if (part.literal)
				builder.appendSource(quote, quote + 1);
			builder.appendSynthetic(")", quote);
		}
		builder.appendSynthetic(")", quote);
		return true;
	}
};
} // namespace

CodeLine *expandFormattedStringLine(ParseContext &context, CodeLine &line) {
	return FormattedStringExpansion(context, line).expand();
}
