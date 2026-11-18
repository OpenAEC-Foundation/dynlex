fs::path compilerOutputPath(const ParseContext &context) {
	return fs::absolute(context.options.emitLLVM ? llvmOutputPath(context.options) : nativeExecutablePath(context.options));
}

Json compilerOutputIdentity(const ParseContext &context) {
	if (context.options.emitLLVM && llvmOutputIsStdout(context.options))
		return Json{{"path", "-"}, {"digest", ""}};
	fs::path path = compilerOutputPath(context);
	requireCompilerInvariant(fs::is_regular_file(path), "successful output producer produced no artifact");
	return Json{{"path", path.string()}, {"digest", digestFile(path)}};
}

bool compilerOutputUnchanged(const ParseContext &context, const Json &previous) {
	if (context.options.emitLLVM && llvmOutputIsStdout(context.options))
		return false;
	const Json &identity = previous.at("compiler_output");
	fs::path path = compilerOutputPath(context);
	return path.string() == identity.at("path").get<std::string>() && fs::is_regular_file(path) &&
		   digestFile(path) == identity.at("digest").get<std::string>();
}
