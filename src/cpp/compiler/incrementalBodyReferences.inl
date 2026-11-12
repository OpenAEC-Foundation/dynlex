// A body owns compact reference tables; repeated source paths and lines are
// encoded once instead of being copied into every expression and diagnostic.
class InferenceBodyReferenceWriter {
  public:
	explicit InferenceBodyReferenceWriter(InferenceBodySources &sources) : sources(sources) {}
	Json section(const Section *value) { return intern("sections", sources.section(value)); }
	Json definition(const PatternDefinition *value) {
		Json record = sources.definition(value);
		if (!record.is_null())
			record.at(0) = intern("sections", record.at(0));
		return intern("definitions", std::move(record));
	}
	Json declaration(const VariableReference *value) {
		Json record = sources.declaration(value);
		if (!record.is_null())
			record.at(0) = intern("sections", record.at(0));
		return intern("declarations", std::move(record));
	}
	Json range(const Range &value) {
		if (!value.line)
			return nullptr;
		return Json::array({line(value.line), value.start(), value.end()});
	}
	Json node(const PatternTreeNode *value) { return intern("nodes", sources.node(value)); }
	Json expressionSource(Json value) { return intern("expression_sources", anchor(std::move(value))); }
	Json take() { return std::move(records); }

  private:
	Json anchor(Json value) {
		value.at(0) = intern("sections", value.at(0));
		return value;
	}
	Json line(const CodeLine *value) {
		if (auto found = lines.find(value); found != lines.end())
			return found->second;
		Json record = sources.line(value);
		if (record.contains("source"))
			record.at("source") = anchor(std::move(record.at("source")));
		else {
			record.at("section") = intern("sections", record.at("section"));
			record.at("opening") = intern("sections", record.at("opening"));
			record.at("origin") = anchor(std::move(record.at("origin")));
			for (Json &slice : record.at("slices"))
				slice.at(2) = anchor(std::move(slice.at(2)));
		}
		Json id = intern("lines", std::move(record));
		lines.emplace(value, id);
		return id;
	}
	Json intern(const std::string &table, Json value) {
		if (value.is_null())
			return nullptr;
		auto &indices = tables[table];
		auto [found, inserted] = indices.try_emplace(value, indices.size());
		if (inserted) {
			if (!records.contains(table))
				records[table] = Json::array();
			records.at(table).push_back(std::move(value));
		}
		return found->second;
	}
	InferenceBodySources &sources;
	std::map<std::string, std::map<Json, size_t>> tables;
	std::map<const CodeLine *, Json> lines;
	Json records = Json::object();
};

class InferenceBodyReferenceReader {
  public:
	explicit InferenceBodyReferenceReader(InferenceBodySources &sources) : sources(sources) {}
	void bind(const Json &value) { records = &value; }
	Section *section(const Json &value) const {
		return pointer("sections", value, sections, [&](const Json &record) {
			return sources.section(record);
		});
	}
	PatternDefinition *definition(const Json &value) const {
		return pointer("definitions", value, definitions, [&](const Json &record) {
			return sources.definition(anchor(record));
		});
	}
	VariableReference *declaration(const Json &value) const {
		return pointer("declarations", value, declarations, [&](const Json &record) {
			return sources.declaration(anchor(record));
		});
	}
	Range range(const Json &value) {
		return value.is_null() ? Range{} : Range(line(value.at(0)), value.at(1).get<int>(), value.at(2).get<int>());
	}
	PatternTreeNode *node(const Json &value) const {
		return pointer("nodes", value, nodes, [&](const Json &record) {
			return sources.node(record);
		});
	}
	Expression *expressionSource(const Json &value, const InferenceStorageIdentities &storage) const {
		return pointer("expression_sources", value, expressions, [&](const Json &record) {
			return storage.decodeExpression(anchor(record));
		});
	}

  private:
	template <class T, class Decode>
	T *pointer(const std::string &table, const Json &value, std::vector<T *> &cache, const Decode &decode) const {
		if (value.is_null())
			return nullptr;
		if (cache.empty())
			cache.resize(records->at(table).size());
		T *&result = cache.at(value.get<size_t>());
		if (!result)
			result = decode(get(table, value));
		return result;
	}
	const Json &get(const std::string &table, const Json &value) const { return records->at(table).at(value.get<size_t>()); }
	Json anchor(Json value) const {
		value.at(0) = get("sections", value.at(0));
		return value;
	}
	CodeLine *line(const Json &value) {
		size_t index = value.get<size_t>();
		if (auto found = lines.find(index); found != lines.end())
			return found->second;
		Json record = get("lines", value);
		if (record.contains("source"))
			record.at("source") = anchor(std::move(record.at("source")));
		else {
			for (const std::string field : {"section", "opening"})
				if (!record.at(field).is_null())
					record.at(field) = get("sections", record.at(field));
			record.at("origin") = anchor(std::move(record.at("origin")));
			for (Json &slice : record.at("slices"))
				slice.at(2) = anchor(std::move(slice.at(2)));
		}
		CodeLine *result = sources.line(record);
		lines.emplace(index, result);
		return result;
	}
	InferenceBodySources &sources;
	const Json *records = nullptr;
	std::map<size_t, CodeLine *> lines;
	mutable std::vector<Section *> sections;
	mutable std::vector<PatternDefinition *> definitions;
	mutable std::vector<VariableReference *> declarations;
	mutable std::vector<PatternTreeNode *> nodes;
	mutable std::vector<Expression *> expressions;
};
