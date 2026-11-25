static void addLibraryDiagnosticData(const ::Diagnostic &diag, Diagnostic &lspDiag) {
	if (diag.configuredMessage && diag.configuredMessage->key == "failed to import source file") {
		for (const auto &[key, path] : diag.configuredMessage->placeholders) {
			if (key != "path")
				continue;
			auto slash = path.find('/');
			if (slash == std::string::npos || slash == 0)
				continue;
			auto alias = path.substr(0, slash);
			if (alias == "lib" || alias == "packages" || alias == "site" || alias.front() < 'a' || alias.front() > 'z')
				continue;
			if (!std::all_of(alias.begin(), alias.end(), [](char c) {
				return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
			}))
				continue;
			if (!lspDiag.data)
				lspDiag.data = Json::object();
			(*lspDiag.data)["libraryAlias"] = alias;
		}
	}
}
