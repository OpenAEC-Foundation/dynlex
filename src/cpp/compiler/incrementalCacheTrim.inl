struct CacheTrimWork {
	size_t entriesScanned = 0;
	size_t entriesSorted = 0;
	size_t entriesRemoved = 0;
	double scanMilliseconds = 0;
	double orderMilliseconds = 0;
};

CacheTrimWork trimCache(const fs::path &directory) {
	CacheTrimWork work;
	const auto scanStart = Clock::now();
	struct Entry {
		std::string name;
		uintmax_t size;
		llvm::sys::TimePoint<> time;
	};
	std::vector<Entry> entries;
	uintmax_t size = 0;
	constexpr std::array<std::string_view, 15> prefixes{
		"object-",
		"program-",
		"payload-",
		"matcher-paths-",
		"matcher-segment-",
		"matcher-",
		parserIndexPrefix,
		"parser-",
		"errors-",
		"optimization-input-",
		"optimization-",
		"native-unit-",
		"inference-summary-",
		"inference-body-",
		"inference-class-"
	};
	const std::u8string utf8Directory = directory.u8string();
	const llvm::StringRef directoryBytes(reinterpret_cast<const char *>(utf8Directory.data()), utf8Directory.size());
	std::error_code iterationError;
	for (llvm::sys::fs::directory_iterator file(directoryBytes, iterationError), end; file != end;
		 file.increment(iterationError)) {
		if (iterationError)
			throw fs::filesystem_error("cannot inspect cache directory", directory, iterationError);
		const llvm::StringRef filename = llvm::sys::path::filename(file->path());
		std::string_view name(filename.data(), filename.size());
		if (!name.ends_with(".entry"))
			continue;
		name.remove_suffix(6);
		auto prefix = std::find_if(prefixes.begin(), prefixes.end(), [&](std::string_view value) {
			return name.starts_with(value);
		});
		if (prefix == prefixes.end() || name.size() != prefix->size() + 64 ||
			!std::all_of(name.begin() + prefix->size(), name.end(), [](char character) {
			return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f');
		}))
			continue;
		llvm::sys::fs::file_status metadata;
		std::error_code error = llvm::sys::fs::status(file->path(), metadata);
		if (error == std::errc::no_such_file_or_directory)
			continue;
		if (error)
			throw fs::filesystem_error("cannot inspect cache metadata", directory / filename.str(), error);
		if (!llvm::sys::fs::is_regular_file(metadata)) {
			auto reason =
				llvm::sys::fs::is_directory(metadata) ? std::errc::is_a_directory : std::errc::operation_not_supported;
			throw fs::filesystem_error("cannot inspect cache size", directory / filename.str(), std::make_error_code(reason));
		}
		entries.push_back({filename.str(), metadata.getSize(), metadata.getLastModificationTime()});
		size += metadata.getSize();
	}
	if (iterationError)
		throw fs::filesystem_error("cannot inspect cache directory", directory, iterationError);
	work.entriesScanned = entries.size();
	work.scanMilliseconds = elapsedMilliseconds(scanStart);
	if (size <= maximumCacheBytes)
		return work;
	const auto orderStart = Clock::now();
	std::sort(entries.begin(), entries.end(), [](const Entry &left, const Entry &right) {
		return left.time == right.time ? left.name < right.name : left.time < right.time;
	});
	work.entriesSorted = entries.size();
	work.orderMilliseconds = elapsedMilliseconds(orderStart);
	for (const Entry &entry : entries) {
		if (size <= maximumCacheBytes)
			break;
		fs::remove(directory / entry.name);
		size -= entry.size;
		++work.entriesRemoved;
	}
	return work;
}
