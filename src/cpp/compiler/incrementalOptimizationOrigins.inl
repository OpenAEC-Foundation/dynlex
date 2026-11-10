// An optimized unit and its final symbol bindings identify the body query's
// inputs. Fingerprints are computed only for live bodies with changed inputs.
struct OptimizationOutput {
	std::string bitcode;
	Json functions;
};

constexpr std::string_view optimizationOutputHeader = "DynLex optimized unit v1\n";

OptimizationOutput decodeOptimizationOutput(std::string_view payload) {
	if (!payload.starts_with(optimizationOutputHeader))
		throw std::runtime_error("invalid optimization output header");
	size_t end = payload.find('\n', optimizationOutputHeader.size());
	if (end == std::string_view::npos)
		throw std::runtime_error("missing optimization output records");
	return {
		std::string(payload.substr(end + 1)),
		Json::parse(payload.substr(optimizationOutputHeader.size(), end - optimizationOutputHeader.size()))
	};
}

struct OptimizationOrigins {
	struct Candidate {
		std::string inputKey;
		const Json *references;
		const std::set<std::string> *unitSymbols;
	};
	std::map<std::string, std::vector<Candidate>> functions;
	std::map<std::string, size_t> occurrences;
	std::set<std::string> locals;
	std::map<std::string, std::set<std::string>> unitSymbols;

	void capture(
		const llvm::Module &module, const std::string &name, const std::string &identity,
		const std::set<std::string> &owned, const Json &records
	) {
		std::set<std::string> &symbols = unitSymbols[name];
		for (const llvm::GlobalValue &value : module.global_values()) {
			std::string symbol = value.getName().str();
			symbols.insert(symbol);
			occurrences[symbol]++;
			if (value.hasLocalLinkage())
				locals.insert(symbol);
		}
		for (const llvm::Function &function : module) {
			std::string symbol = function.getName().str();
			if (function.isDeclarationForLinker() || (!function.hasLocalLinkage() && !owned.contains(symbol)))
				continue;
			functions[symbol].push_back({digest(identity + symbol), &records.at(symbol), &symbols});
		}
	}

	void
	bind(const llvm::Module &module, const std::map<std::string, std::string> &renames, BuildStatistics &statistics) const {
		// LLVM can rename foo to foo.1 and then an original foo.1 to foo.1.2.
		// Any private collision invalidates name-based origins for that unit.
		std::set<const std::set<std::string> *> ambiguousUnits;
		for (const auto &[_, symbols] : unitSymbols)
			if (std::ranges::any_of(symbols, [&](const std::string &symbol) {
				return locals.contains(symbol) && occurrences.at(symbol) > 1;
			}))
				ambiguousUnits.insert(&symbols);
		std::map<std::string, std::string> canonicalNames;
		for (const auto &[canonical, current] : renames)
			canonicalNames.emplace(current, canonical);
		for (const llvm::Function &function : module) {
			std::string name = function.getName().str();
			const std::string &canonical = canonicalNames.at(name);
			auto found = functions.find(canonical);
			if (function.isDeclaration() || found == functions.end() || found->second.size() != 1)
				continue;
			const Candidate &candidate = found->second.front();
			if (ambiguousUnits.contains(candidate.unitSymbols))
				continue;
			OptimizedFunctionOrigin origin{candidate.inputKey, {}};
			auto include = [&](const std::string &reference) {
				auto renamed = renames.find(reference);
				origin.bindings.emplace(
					reference, renamed == renames.end() ? std::nullopt : std::optional<std::string>(renamed->second)
				);
			};
			include(canonical);
			for (const Json &reference : *candidate.references)
				include(reference.get_ref<const std::string &>());
			statistics.optimizedFunctionOrigins.emplace(name, std::move(origin));
		}
	}
};
