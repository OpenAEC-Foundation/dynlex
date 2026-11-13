void trimCache(const fs::path &directory) {
	struct Entry {
		fs::path path;
		uintmax_t size;
		llvm::sys::TimePoint<> time;
	};
	std::vector<Entry> entries;
	uintmax_t size = 0;
	constexpr std::array<std::string_view, 14> prefixes{"object-",		   "program-",		   "payload-",
														"matcher-paths-",  "matcher-segment-", "matcher-",
														"parser-",		   "errors-",		   "optimization-input-",
														"optimization-",   "native-unit-",	   "inference-summary-",
														"inference-body-", "inference-class-"};
	for (const auto &file : fs::directory_iterator(directory)) {
		if (file.path().extension() != ".entry")
			continue;
		std::string name = file.path().stem().string();
		auto prefix = std::find_if(prefixes.begin(), prefixes.end(), [&](std::string_view value) {
			return name.starts_with(value);
		});
		if (prefix == prefixes.end() || name.size() != prefix->size() + 64 ||
			!std::all_of(name.begin() + prefix->size(), name.end(), [](char character) {
			return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f');
		}))
			continue;
#ifdef _WIN32
		const std::u8string utf8Path = file.path().u8string();
		const llvm::StringRef pathBytes(reinterpret_cast<const char *>(utf8Path.data()), utf8Path.size());
#else
		const llvm::StringRef pathBytes(file.path().native());
#endif
		llvm::sys::fs::file_status metadata;
		std::error_code error = llvm::sys::fs::status(pathBytes, metadata);
		if (error == std::errc::no_such_file_or_directory)
			continue;
		if (error)
			throw fs::filesystem_error("cannot inspect cache metadata", file.path(), error);
		if (!llvm::sys::fs::is_regular_file(metadata)) {
			auto reason =
				llvm::sys::fs::is_directory(metadata) ? std::errc::is_a_directory : std::errc::operation_not_supported;
			throw fs::filesystem_error("cannot inspect cache size", file.path(), std::make_error_code(reason));
		}
		entries.push_back({file.path(), metadata.getSize(), metadata.getLastModificationTime()});
		size += metadata.getSize();
	}
	std::sort(entries.begin(), entries.end(), [](const Entry &left, const Entry &right) {
		return left.time == right.time ? left.path < right.path : left.time < right.time;
	});
	for (const Entry &entry : entries) {
		if (size <= maximumCacheBytes)
			break;
		fs::remove(entry.path);
		size -= entry.size;
	}
}
