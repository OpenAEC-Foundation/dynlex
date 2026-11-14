// Both projections consume the same value stream. Fingerprinting never creates
// the nested JSON arrays used by a declaration record.
class DeclarationJsonWriter {
	Json result;
	std::vector<Json *> containers;
	std::string field;

	Json &append(Json value) {
		if (containers.empty())
			return result = std::move(value);
		if (containers.back()->is_array())
			return containers.back()->emplace_back(std::move(value));
		return (*containers.back())[std::exchange(field, {})] = std::move(value);
	}

  public:
	void object(size_t) { containers.push_back(&append(Json::object())); }
	void array(size_t size) {
		Json &value = append(Json::array());
		value.get_ref<Json::array_t &>().reserve(size);
		containers.push_back(&value);
	}
	void end() { containers.pop_back(); }
	void key(std::string_view name) { field = name; }
	template <typename Value> void value(Value &&value) { append(Json(std::forward<Value>(value))); }
	Json finish() { return std::move(result); }
};

class DeclarationFingerprintWriter {
	std::string bytes;
	void number(std::uint64_t value) {
		for (size_t index = 0; index < 8; index++) {
			bytes.push_back(static_cast<char>(value & 0xff));
			value >>= 8;
		}
	}
	void text(char kind, std::string_view value) {
		bytes.push_back(kind);
		number(value.size());
		bytes.append(value);
	}

  public:
	DeclarationFingerprintWriter() { bytes.reserve(2048); }
	void object(size_t size) {
		bytes.push_back('O');
		number(size);
	}
	void array(size_t size) {
		bytes.push_back('A');
		number(size);
	}
	void end() {}
	void key(std::string_view name) { text('K', name); }
	void value(std::string_view value) { text('S', value); }
	void value(const std::string &value) { this->value(std::string_view(value)); }
	void value(bool value) { bytes.push_back(value ? 'T' : 'F'); }
	void value(int value) {
		bytes.push_back('I');
		number(static_cast<std::uint64_t>(value));
	}
	void value(const Json &value) { text('J', value.dump()); }
	std::string finish() const { return digest(bytes); }
};

template <typename Writer>
void writeResolvedDefinitionElements(Writer &writer, const std::vector<DefinitionPatternElement> &elements) {
	writer.array(elements.size());
	for (const DefinitionPatternElement &element : elements) {
		writer.array(8);
		writer.value(static_cast<int>(element.type));
		writer.value(element.text);
		writer.value(element.isArgumentPack);
		writer.value(element.typeConstraintName);
		writer.value(element.promotedFromVariableLike);
		writer.value(element.isExplicitLiteral);
		writer.value(element.isLiteralInSingleWordSpelling);
		writer.array(element.alternatives.size());
		for (const auto &alternative : element.alternatives)
			writeResolvedDefinitionElements(writer, alternative);
		writer.end();
		writer.end();
	}
	writer.end();
}

template <typename Writer> void writeSectionDeclaration(Writer &writer, const Section &section) {
	writer.object(11);
	writer.key("identity");
	writer.value(sectionIdentity(section));
	writer.key("definitions");
	writer.array(section.patternDefinitions.size());
	for (const PatternDefinition *definition : section.patternDefinitions) {
		writer.object(2);
		writer.key("spelling");
		writer.value(definition->toString());
		writer.key("elements");
		writeResolvedDefinitionElements(writer, definition->patternElements);
		writer.end();
	}
	writer.end();
	writer.key("local");
	writer.value(section.isLocal);
	writer.key("flex");
	writer.value(section.isFlex);
	writer.key("exposed");
	writer.value(section.isExposed);
	writer.key("conversion");
	writer.value(section.isConversion);
	writer.key("implicit_conversion");
	writer.value(section.isImplicitConversion);
	writer.key("return_contract");
	writer.value(static_cast<int>(section.returnContract));
	writer.key("before");
	writer.array(section.beforePatterns.size());
	for (const std::string &pattern : section.beforePatterns)
		writer.value(pattern);
	writer.end();
	writer.key("after");
	writer.array(section.afterPatterns.size());
	for (const std::string &pattern : section.afterPatterns)
		writer.value(pattern);
	writer.end();
	writer.key("associativity");
	writer.value(static_cast<int>(section.associativity));
	writer.end();
}
