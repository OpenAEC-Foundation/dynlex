#pragma once
#include "fileSystem.h"
#include <nlohmann/json.hpp>
#include <unordered_map>

// Records immutable source inputs, including unsuccessful lookups, independently
// of whether their provider is the editor, local disk or a virtual filesystem.
class RecordingFileSystem : public lsp::FileSystem {
  public:
	RecordingFileSystem();
	explicit RecordingFileSystem(std::unique_ptr<lsp::FileSystem> files);
	lsp::SourceFile *getFile(const std::string &path, lsp::FileReadPurpose purpose = lsp::FileReadPurpose::Source) override;
	const nlohmann::json &observe(const std::string &path);
	lsp::SourceFile *cachedFile(const std::string &path);
	bool matches(const nlohmann::json &previous);
	size_t observationRequests() const { return requests; }
	size_t observationsComputed() const { return computed; }
	size_t observationsReused() const { return reused; }
	const std::string &sourceContentDigest(const lsp::SourceFile &file) const;
	nlohmann::json observations = nlohmann::json::object();
	nlohmann::json configurationObservations = nlohmann::json::object();

  private:
	std::unique_ptr<lsp::FileSystem> files;
	std::unordered_map<const lsp::SourceFile *, nlohmann::json> sourceObservations;
	size_t requests = 0, computed = 0, reused = 0;
};
