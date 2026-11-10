Json linkedOutputIdentity(const ParseContext &context) {
	fs::path path = fs::absolute(nativeExecutablePath(context.options));
	requireCompilerInvariant(fs::is_regular_file(path), "successful native link produced no executable");
	return Json{{"path", path.string()}, {"digest", digestFile(path)}};
}

bool linkedOutputUnchanged(const ParseContext &context, const Json &previous) {
	const Json &identity = previous.at("linked_output");
	fs::path path = fs::absolute(nativeExecutablePath(context.options));
	return path.string() == identity.at("path").get<std::string>() && fs::is_regular_file(path) &&
		   digestFile(path) == identity.at("digest").get<std::string>();
}
