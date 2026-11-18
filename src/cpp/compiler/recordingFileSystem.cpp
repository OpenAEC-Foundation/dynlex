#include "recordingFileSystem.h"
#include "sourceIdentity.h"

RecordingFileSystem::RecordingFileSystem() : RecordingFileSystem(std::make_unique<lsp::LocalFileSystem>()) {}
RecordingFileSystem::RecordingFileSystem(std::unique_ptr<lsp::FileSystem> files) : files(std::move(files)) {}

lsp::SourceFile *RecordingFileSystem::getFile(const std::string &path, lsp::FileReadPurpose purpose) {
	requests++;
	auto &observed = purpose == lsp::FileReadPurpose::SyntaxConfiguration ? configurationObservations : observations;
	lsp::SourceFile *file = files->getFile(path, purpose);
	if (!file) {
		observed[path] = nullptr;
		return nullptr;
	}
	auto [entry, inserted] = sourceObservations.try_emplace(file);
	if (inserted) {
		entry->second = nlohmann::json{
			{"content", digest(file->content)}, {"canonical", file->canonicalPath}, {"internal", file->isInternal}
		};
		computed++;
	} else
		reused++;
	observed[path] = entry->second;
	return file;
}

const nlohmann::json &RecordingFileSystem::observe(const std::string &path) {
	getFile(path);
	return observations.at(path);
}

lsp::SourceFile *RecordingFileSystem::cachedFile(const std::string &path) { return files->getFile(path); }

bool RecordingFileSystem::matches(const nlohmann::json &previous) {
	for (const auto &[path, observation] : previous.items())
		if (observe(path) != observation)
			return false;
	return true;
}

const std::string &RecordingFileSystem::sourceContentDigest(const lsp::SourceFile &file) const {
	return sourceObservations.at(&file).at("content").get_ref<const std::string &>();
}
