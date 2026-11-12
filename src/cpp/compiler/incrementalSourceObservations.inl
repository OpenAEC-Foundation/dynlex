// Native imports hold immutable source snapshots for one compilation. Multiple
// import paths and cache checks can consume the same observation without hashing
// or canonicalizing that loaded file again. Missing paths remain live lookups.
class RecordingFileSystem : public lsp::FileSystem {
  public:
	lsp::SourceFile *getFile(const std::string &path) override {
		requests++;
		lsp::SourceFile *file = files.getFile(path);
		if (!file) {
			observations[path] = nullptr;
			return nullptr;
		}
		auto [entry, inserted] = sourceObservations.try_emplace(file);
		if (inserted) {
			entry->second = Json{
				{"content", digest(file->content)},
				{"canonical", fs::canonical(pathutil::toFilesystemPath(path)).string()},
				{"internal", file->isInternal}
			};
			computed++;
		} else
			reused++;
		observations[path] = entry->second;
		return file;
	}
	const Json &observe(const std::string &path) {
		getFile(path);
		return observations.at(path);
	}
	lsp::SourceFile *cachedFile(const std::string &path) { return files.getFile(path); }
	size_t observationRequests() const { return requests; }
	size_t observationsComputed() const { return computed; }
	size_t observationsReused() const { return reused; }
	Json observations = Json::object();

  private:
	lsp::LocalFileSystem files;
	std::unordered_map<const lsp::SourceFile *, Json> sourceObservations;
	size_t requests = 0, computed = 0, reused = 0;
};
