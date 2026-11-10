std::string digestFile(const fs::path &path) {
	std::ifstream input(path, std::ios::binary);
	if (!input)
		throw std::runtime_error("cannot read " + path.string());
	llvm::SHA256 hash;
	std::array<char, 65536> buffer;
	while (input.read(buffer.data(), buffer.size()) || input.gcount())
		hash.update(llvm::StringRef(buffer.data(), input.gcount()));
	if (!input.eof())
		throw std::runtime_error("failed reading " + path.string());
	return llvm::toHex(hash.final(), true);
}

// Linker build IDs identify the complete linked compiler without hashing its
// debug sections on every invocation. Formats without a build ID use the
// exact executable content digest instead.
std::string compilerBuildIdentity(const std::string &argv0) {
	std::string executable = llvm::sys::fs::getMainExecutable(argv0.c_str(), reinterpret_cast<void *>(&compileIncrementally));
	if (executable.empty())
		throw std::runtime_error("cannot identify compiler executable");
	auto object = llvm::object::ObjectFile::createObjectFile(executable);
	if (!object)
		throw std::runtime_error("cannot inspect compiler executable: " + llvm::toString(object.takeError()));
	llvm::object::BuildIDRef buildId = llvm::object::getBuildID(object->getBinary());
	if (!buildId.empty())
		return "build-id:" + llvm::toHex(buildId, true);
	return "sha256:" + digestFile(executable);
}
