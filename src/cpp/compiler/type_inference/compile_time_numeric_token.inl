static std::optional<CompileTimeValue> parseCompileTimeNumericToken(std::string_view token) {
	NumericLiteralParseResult parsed = parseNumericLiteral(token);
	if (!parsed)
		return std::nullopt;
	return numericLiteralCompileTimeValue(parsed.value);
}
