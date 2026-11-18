fs::path compilerOutputPath(const ParseContext &context) {
	const auto &options = context.options;
	if (options.emitLLVM)
		return fs::absolute(llvmOutputPath(options));
	if (options.emitObject)
		return fs::absolute(nativeObjectPath(options));
	if (options.emitWASM)
		return fs::absolute(wasmOutputPath(options));
	return fs::absolute(nativeExecutablePath(options));
}

bool compilerOutputIsStdout(const ParseContext &context) {
	const auto &options = context.options;
	return (options.emitLLVM || options.emitObject || options.emitWASM) && options.outputPath == "-";
}

Json compilerOutputIdentity(const ParseContext &context) {
	if (compilerOutputIsStdout(context))
		return Json{{"path", "-"}, {"digest", ""}};
	fs::path path = compilerOutputPath(context);
	requireCompilerInvariant(fs::is_regular_file(path), "successful output producer produced no artifact");
	return Json{{"path", path.string()}, {"digest", digestFile(path)}};
}

bool compilerOutputUnchanged(const ParseContext &context, const Json &previous) {
	if (compilerOutputIsStdout(context))
		return false;
	const Json &identity = previous.at("compiler_output");
	fs::path path = compilerOutputPath(context);
	return path.string() == identity.at("path").get<std::string>() && fs::is_regular_file(path) &&
		   digestFile(path) == identity.at("digest").get<std::string>();
}
