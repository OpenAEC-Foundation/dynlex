void trimCache(const fs::path &directory) {
	struct Entry {
		fs::path path;
		uintmax_t size;
		fs::file_time_type time;
	};
	std::vector<Entry> entries;
	uintmax_t size = 0;
	constexpr std::array<std::string_view, 13> prefixes{
		"object-",
		"program-",
		"payload-",
		"matcher-paths-",
		"matcher-",
		"parser-",
		"errors-",
		"optimization-input-",
		"optimization-",
		"native-unit-",
		"inference-summary-",
		"inference-body-",
		"inference-class-"
	};
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
