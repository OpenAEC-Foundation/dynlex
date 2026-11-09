void trimCache(const fs::path &directory) {
	struct Entry {
		fs::path path;
		uintmax_t size;
		fs::file_time_type time;
	};
	std::vector<Entry> entries;
	uintmax_t size = 0;
	for (const auto &file : fs::directory_iterator(directory)) {
		if (file.path().extension() != ".entry")
			continue;
		std::string name = file.path().stem().string();
		size_t prefixLength = name.starts_with("object-")	 ? 7
							  : name.starts_with("program-") ? 8
							  : name.starts_with("payload-") ? 8
							  : name.starts_with("matcher-") ? 8
							  : name.starts_with("parser-")	 ? 7
							  : name.starts_with("errors-")	 ? 7
														 : 0;
		if (!prefixLength || name.size() != prefixLength + 64 ||
			!std::all_of(name.begin() + prefixLength, name.end(), [](char character) {
			return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f');
		}))
			continue;
		std::error_code error;
		uintmax_t entrySize = file.file_size(error);
		if (error == std::errc::no_such_file_or_directory)
			continue;
		if (error)
			throw fs::filesystem_error("cannot inspect cache size", file.path(), error);
		auto time = file.last_write_time(error);
		if (error == std::errc::no_such_file_or_directory)
			continue;
		if (error)
			throw fs::filesystem_error("cannot inspect cache age", file.path(), error);
		entries.push_back({file.path(), entrySize, time});
		size += entrySize;
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
