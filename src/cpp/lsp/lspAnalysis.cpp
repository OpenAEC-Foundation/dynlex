#include "lspAnalysis.h"
#include "pathUtils.h"
#include "patternDefinition.h"
#include "section.h"
#include "sourceFile.h"
#include <algorithm>
#include <stdexcept>
#include <string_view>

namespace lsp {

std::vector<ParseContext::Options> parseAnalysisProfiles(const std::optional<Json> &initializationOptions) {
	if (!initializationOptions || !initializationOptions->contains("dynlex"))
		return {ParseContext::Options{}};

	const Json &dynlexOptions = initializationOptions->at("dynlex");
	if (!dynlexOptions.is_object() || !dynlexOptions.contains("analysisProfiles"))
		throw std::invalid_argument("DynLex initialization options require analysisProfiles");
	const Json &profiles = dynlexOptions.at("analysisProfiles");
	if (!profiles.is_array() || profiles.empty())
		throw std::invalid_argument("DynLex analysisProfiles must be a non-empty array");

	std::vector<ParseContext::Options> result;
	std::unordered_set<std::string> profileKeys;
	for (const Json &profile : profiles) {
		if (!profile.is_object() || !profile.contains("target") || !profile.at("target").is_string())
			throw std::invalid_argument("Each DynLex analysis profile requires a target");

		ParseContext::Options options;
		const std::string target = profile.at("target").get<std::string>();
		std::string profileKey = target;
		if (target == "cpu") {
			if (profile.contains("shaderStage"))
				throw std::invalid_argument("A CPU analysis profile cannot specify shaderStage");
		} else if (target == "spirv") {
			if (!profile.contains("shaderStage") || !profile.at("shaderStage").is_string())
				throw std::invalid_argument("A SPIR-V analysis profile requires shaderStage");
			const std::string stage = profile.at("shaderStage").get<std::string>();
			options.emitSPIRV = true;
			if (stage == "fragment") {
				options.shaderStage = ParseContext::ShaderStage::Fragment;
			} else if (stage == "vertex") {
				options.shaderStage = ParseContext::ShaderStage::Vertex;
			} else {
				throw std::invalid_argument("DynLex shaderStage must be fragment or vertex");
			}
			profileKey += ":" + stage;
		} else {
			throw std::invalid_argument("DynLex analysis target must be cpu or spirv");
		}

		if (!profileKeys.insert(profileKey).second)
			throw std::invalid_argument("DynLex analysis profiles must be unique");
		result.push_back(std::move(options));
	}
	return result;
}

void eraseImportTrackingForMain(ImportGraph &graph, const std::string &mainUri) {
	for (auto it = graph.begin(); it != graph.end();) {
		it->second.erase(mainUri);
		if (it->second.empty())
			it = graph.erase(it);
		else
			++it;
	}
}

void addImportTrackingForMain(ImportGraph &graph, const std::string &mainUri, const ParseContext &context) {
	for (const auto &[path, sourceFile] : context.importedFiles) {
		(void)path;
		if (!sourceFile)
			continue;
		const std::string importedUri = pathutil::toAbsoluteUri(sourceFile->uri);
		if (importedUri != mainUri)
			graph[importedUri].insert(mainUri);
	}
}

std::vector<ParseContext *>
findContextsForUri(const std::string &uri, const ParseContextMap &contexts, const ImportGraph &graph) {
	struct Candidate {
		ParseContext *context{};
		std::string mainUri;
		size_t profileIndex = 0;
		bool fromImporter = false;
		int score = -1;
	};

	auto scoreContextForUri = [&](ParseContext *context) {
		if (!context || !context->mainSection)
			return -1;
		int score = 0;
		std::vector<Section *> stack{context->mainSection};
		while (!stack.empty()) {
			Section *section = stack.back();
			stack.pop_back();
			if (!section)
				continue;
			for (Section *child : section->children) {
				if (child)
					stack.push_back(child);
			}

			const bool hasDefinitionInUri = std::any_of(
				section->patternDefinitions.begin(), section->patternDefinitions.end(),
				[&](PatternDefinition *def) {
				return def && def->range.line && def->range.line->sourceFile &&
					   pathutil::toAbsoluteUri(def->range.line->sourceFile->uri) == uri;
			}
			);
			if (!hasDefinitionInUri)
				continue;

			const int instantiationCount = static_cast<int>(section->instantiations.size());
			if (instantiationCount > 1)
				score += 1000 + instantiationCount;
			else if (instantiationCount == 1)
				score += 10;
		}
		return score;
	};

	std::vector<Candidate> candidates;
	auto addMainContexts = [&](const std::string &mainUri, bool fromImporter) {
		auto contextIt = contexts.find(mainUri);
		if (contextIt == contexts.end())
			return;
		for (size_t profileIndex = 0; profileIndex < contextIt->second.size(); ++profileIndex) {
			ParseContext *context = contextIt->second[profileIndex].get();
			candidates.push_back({context, mainUri, profileIndex, fromImporter, scoreContextForUri(context)});
		}
	};

	addMainContexts(uri, false);
	auto importIt = graph.find(uri);
	if (importIt != graph.end()) {
		for (const std::string &mainUri : importIt->second)
			addMainContexts(mainUri, true);
	}

	std::sort(candidates.begin(), candidates.end(), [](const Candidate &left, const Candidate &right) {
		if (left.score != right.score)
			return left.score > right.score;
		if (left.fromImporter != right.fromImporter)
			return left.fromImporter;
		if (left.mainUri != right.mainUri)
			return left.mainUri < right.mainUri;
		return left.profileIndex < right.profileIndex;
	});

	std::vector<ParseContext *> result;
	result.reserve(candidates.size());
	for (const Candidate &candidate : candidates)
		result.push_back(candidate.context);
	return result;
}

} // namespace lsp
