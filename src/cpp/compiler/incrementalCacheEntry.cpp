#include "incrementalCacheEntry.h"
#include "compilerUtils.h"
#include "stableIdentity.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#ifdef _WIN32
#include "../../runtime/windowsFileDisposition.h"
#include "llvm/Support/Windows/WindowsSupport.h"
#include "llvm/Support/WindowsError.h"
#endif
#include <algorithm>
#include <array>
#include <chrono>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;
namespace llvmfs = llvm::sys::fs;
using Clock = std::chrono::steady_clock;
constexpr uint64_t maximumCacheBytes = 512 * 1024 * 1024;
thread_local IncrementalCacheDirectory *activeDirectory = nullptr;

std::string utf8Path(const fs::path &path) {
	const auto utf8 = path.u8string();
	return {reinterpret_cast<const char *>(utf8.data()), utf8.size()};
}

struct DirectoryStamp {
	uint64_t device = 0;
	uint64_t file = 0;
	uint64_t modification = 0;
	bool operator==(const DirectoryStamp &) const = default;
};

DirectoryStamp directoryStamp(const fs::path &path) {
	llvmfs::file_status status;
	if (auto error = llvmfs::status(utf8Path(path), status))
		throw fs::filesystem_error("cannot inspect cache directory", path, error);
	const auto identity = status.getUniqueID();
	return {
		identity.getDevice(), identity.getFile(),
		static_cast<uint64_t>(status.getLastModificationTime().time_since_epoch().count())
	};
}

std::optional<llvmfs::file_status> entryStatus(const fs::path &path, llvm::StringRef name) {
	llvmfs::file_status status;
	if (auto error = llvmfs::status(name, status)) {
		if (error == std::errc::no_such_file_or_directory)
			return std::nullopt;
		throw fs::filesystem_error("cannot inspect cache metadata", path, error);
	}
	if (!llvmfs::is_regular_file(status)) {
		auto reason = llvmfs::is_directory(status) ? std::errc::is_a_directory : std::errc::operation_not_supported;
		throw fs::filesystem_error("cannot inspect cache size", path, std::make_error_code(reason));
	}
	return status;
}

struct CacheCheckpoint {
	uint64_t version = 0;
	DirectoryStamp stamp;
	uint64_t bytes = 0;
};
constexpr uint64_t checkpointVersion = 1;

struct CacheCheckpointRecord {
	CacheCheckpoint checkpoint;
	std::array<char, 64> digest{};
};
static_assert(sizeof(CacheCheckpoint) == 5 * sizeof(uint64_t));
static_assert(sizeof(CacheCheckpointRecord) == sizeof(CacheCheckpoint) + 64);

std::string checkpointDigest(const CacheCheckpoint &checkpoint) {
	return stableIdentityDigest({reinterpret_cast<const char *>(&checkpoint), sizeof(checkpoint)});
}

void checkStream(llvm::raw_fd_ostream &stream, const char *operation, const fs::path &path) {
	if (stream.has_error()) {
		auto error = stream.error();
		stream.clear_error();
		throw fs::filesystem_error(operation, path, error);
	}
}

llvmfs::FileLocker lockCheckpoint(llvm::raw_fd_stream &control, const fs::path &path) {
	auto locked = control.lock();
	if (!locked)
		throw fs::filesystem_error("cannot lock cache checkpoint", path, llvm::errorToErrorCode(locked.takeError()));
	return std::move(*locked);
}

bool isCacheEntryName(std::string_view name) {
	constexpr std::array<std::string_view, 16> prefixes{
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
		"inference-class-",
		"llvm-output-"
	};
	if (!name.ends_with(".entry"))
		return false;
	name.remove_suffix(6);
	auto prefix = std::find_if(prefixes.begin(), prefixes.end(), [&](std::string_view value) {
		return name.starts_with(value);
	});
	return prefix != prefixes.end() && name.size() == prefix->size() + 64 &&
		   std::all_of(name.begin() + prefix->size(), name.end(), [](char character) {
		return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f');
	});
}
} // namespace

IncrementalCacheDirectory::IncrementalCacheDirectory(const fs::path &directory)
	: directory(directory), pending(directory / "publication"), controlPath(pending / "budget") {
	requireCompilerInvariant(!activeDirectory, "nested incremental cache publication");
	fs::create_directories(pending);
	int descriptor;
	if (auto error = llvmfs::openFileForReadWrite(utf8Path(controlPath), descriptor, llvmfs::CD_OpenAlways, llvmfs::OF_None))
		throw fs::filesystem_error("cannot open cache checkpoint", controlPath, error);
	control = std::make_unique<llvm::raw_fd_stream>(descriptor, true);
	control->SetUnbuffered();
	activeDirectory = this;
}

IncrementalCacheDirectory::~IncrementalCacheDirectory() { activeDirectory = nullptr; }

IncrementalCacheDirectory &IncrementalCacheDirectory::current() {
	requireCompilerInvariant(activeDirectory, "cache publication outside incremental compilation");
	return *activeDirectory;
}

std::optional<uint64_t> IncrementalCacheDirectory::checkedSize() {
	CacheCheckpointRecord record;
	control->seek(0);
	auto count = control->read(reinterpret_cast<char *>(&record), sizeof(record));
	checkStream(*control, "cannot read cache checkpoint", controlPath);
	if (count != static_cast<decltype(count)>(sizeof(record)) || record.checkpoint.version != checkpointVersion ||
		checkpointDigest(record.checkpoint) != std::string_view(record.digest.data(), record.digest.size()) ||
		record.checkpoint.stamp != directoryStamp(directory))
		return std::nullopt;
	return record.checkpoint.bytes;
}

void IncrementalCacheDirectory::updateCheckpoint(std::optional<uint64_t> bytes) {
	control->seek(0);
	if (!bytes) {
		// Clear the version before mutating the directory. A killed writer then
		// leaves a checkpoint that the next process can rebuild.
		const uint64_t incomplete = 0;
		control->write(reinterpret_cast<const char *>(&incomplete), sizeof(incomplete));
	} else {
		CacheCheckpointRecord record{{checkpointVersion, directoryStamp(directory), *bytes}};
		const auto digest = checkpointDigest(record.checkpoint);
		std::copy(digest.begin(), digest.end(), record.digest.begin());
		control->write(reinterpret_cast<const char *>(&record), sizeof(record));
	}
	checkStream(*control, "cannot update cache checkpoint", controlPath);
}

void IncrementalCacheDirectory::publish(const fs::path &path, std::string_view payload) {
	requireCompilerInvariant(
		path.parent_path() == directory && isCacheEntryName(path.filename().string()), "invalid cache publication path"
	);
	const uint64_t size = payload.size() + IncrementalCacheEntry::header.size() + 65;
	if (size > IncrementalCacheEntry::maximumBytes)
		throw std::runtime_error("cache entry exceeds the size limit: " + utf8Path(path));
	llvm::SmallString<256> temporary;
	int descriptor;
	if (auto error = llvmfs::createUniqueFile(utf8Path(pending / "entry-%%%%%%"), descriptor, temporary))
		throw fs::filesystem_error("cannot create cache entry", path, error);
	auto cleanup = llvm::scope_exit([&] {
		llvmfs::remove(temporary);
	});
	llvm::raw_fd_ostream output(descriptor, true);
	output << IncrementalCacheEntry::header << stableIdentityDigest(payload) << '\n' << payload;
	output.close();
	checkStream(output, "cannot write cache entry", path);

	std::lock_guard threadLock(publicationMutex);
	auto locked = lockCheckpoint(*control, controlPath);
	auto bytes = checkedSize();
	const auto destination = utf8Path(path);
	uint64_t replacedBytes = 0;
	if (bytes) {
		if (auto replaced = entryStatus(path, destination))
			replacedBytes = replaced->getSize();
	}
	updateCheckpoint(std::nullopt);
	if (auto error = llvmfs::rename(temporary, destination))
		throw fs::filesystem_error("cannot publish cache entry", path, error);
	cleanup.release();
	if (bytes && *bytes >= replacedBytes)
		updateCheckpoint(*bytes - replacedBytes + size);
}

void IncrementalCacheDirectory::remove(const fs::path &path) {
	requireCompilerInvariant(
		path.parent_path() == directory && isCacheEntryName(path.filename().string()), "invalid cache removal path"
	);
	std::lock_guard threadLock(publicationMutex);
	auto locked = lockCheckpoint(*control, controlPath);
	auto removed = entryStatus(path, utf8Path(path));
	if (!removed)
		return;
	auto bytes = checkedSize();
	updateCheckpoint(std::nullopt);
	IncrementalCacheEntry::evict(path);
	if (bytes && *bytes >= removed->getSize())
		updateCheckpoint(*bytes - removed->getSize());
}

CacheTrimWork IncrementalCacheDirectory::trim() {
	std::lock_guard threadLock(publicationMutex);
	auto locked = lockCheckpoint(*control, controlPath);
	CacheTrimWork work;
	if (auto bytes = checkedSize(); bytes && *bytes <= maximumCacheBytes)
		return work;

	const auto scanStart = Clock::now();
	struct Entry {
		fs::path path;
		uint64_t bytes;
		llvm::sys::TimePoint<> time;
	};
	std::vector<Entry> entries;
	uint64_t size = 0;
	const auto directoryBytes = utf8Path(directory);
	auto start = directoryStamp(directory);
	std::error_code iterationError;
	for (llvmfs::directory_iterator file(directoryBytes, iterationError), end; !iterationError && file != end;
		 file.increment(iterationError)) {
		auto filename = llvm::sys::path::filename(file->path());
		if (!isCacheEntryName({filename.data(), filename.size()}))
			continue;
		auto path = directory / filename.str();
		if (auto status = entryStatus(path, file->path())) {
			entries.push_back({std::move(path), status->getSize(), status->getLastModificationTime()});
			size += status->getSize();
		}
	}
	if (iterationError)
		throw fs::filesystem_error("cannot inspect cache directory", directory, iterationError);
	work.entriesScanned = entries.size();
	work.scanMilliseconds = std::chrono::duration<double, std::milli>(Clock::now() - scanStart).count();
	const bool stableScan = start == directoryStamp(directory);
	if (size > maximumCacheBytes) {
		const auto orderStart = Clock::now();
		std::sort(entries.begin(), entries.end(), [](const Entry &left, const Entry &right) {
			return left.time == right.time ? left.path < right.path : left.time < right.time;
		});
		work.entriesSorted = entries.size();
		work.orderMilliseconds = std::chrono::duration<double, std::milli>(Clock::now() - orderStart).count();
		updateCheckpoint(std::nullopt);
		for (const Entry &entry : entries) {
			IncrementalCacheEntry::evict(entry.path);
			size -= entry.bytes;
			++work.entriesRemoved;
			if (size <= maximumCacheBytes)
				break;
		}
	}
	if (stableScan)
		updateCheckpoint(size);
	return work;
}

std::optional<IncrementalCacheEntry> IncrementalCacheEntry::open(const std::filesystem::path &path) {
	const std::u8string utf8 = path.u8string();
	llvm::StringRef filename(reinterpret_cast<const char *>(utf8.data()), utf8.size());
	// Large payloads stay mapped and are decoded only when their stage needs them.
	auto contents = llvm::MemoryBuffer::getFile(filename, false, false);
	if (!contents) {
		if (contents.getError() == std::errc::no_such_file_or_directory)
			return std::nullopt;
		throw std::filesystem::filesystem_error("cannot read cache entry", path, contents.getError());
	}
	if ((*contents)->getBufferSize() > maximumBytes)
		throw std::runtime_error("cache entry exceeds the size limit: " + filename.str());
	return IncrementalCacheEntry(std::move(*contents));
}

void IncrementalCacheEntry::evict(const std::filesystem::path &path) {
	const std::u8string utf8 = path.u8string();
	const llvm::StringRef filename(reinterpret_cast<const char *>(utf8.data()), utf8.size());
	std::error_code error;
#ifdef _WIN32
	llvm::SmallVector<wchar_t, 128> nativePath;
	error = llvm::sys::windows::widenPath(filename, nativePath);
	if (error)
		throw std::filesystem::filesystem_error("cannot open cache entry for eviction", path, error);
	HANDLE file = CreateFileW(
		nativePath.data(), DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
		FILE_FLAG_OPEN_REPARSE_POINT, nullptr
	);
	if (file == INVALID_HANDLE_VALUE) {
		error = llvm::mapWindowsError(GetLastError());
	} else {
		// Delete-on-close leaves the pathname inaccessible while a snapshot is mapped.
		// POSIX disposition unlinks the name while existing mappings retain the bytes.
		if (!dynlex_unlink_windows_file(file, 0))
			error = llvm::mapWindowsError(GetLastError());
		if (auto closeError = llvm::sys::fs::closeFile(file); !error)
			error = closeError;
	}
#else
	error = llvm::sys::fs::remove(filename);
#endif
	if (error && error != std::errc::no_such_file_or_directory)
		throw std::filesystem::filesystem_error("cannot evict cache entry", path, error);
}

IncrementalCacheEntry IncrementalCacheEntry::fromPayload(std::string_view payload) {
	if (payload.size() > maximumBytes - header.size() - 65)
		throw std::runtime_error("cache entry exceeds the size limit");
	std::string bytes;
	bytes.reserve(header.size() + 65 + payload.size());
	bytes.append(header).append(stableIdentityDigest(payload)).append("\n").append(payload);
	return IncrementalCacheEntry(llvm::MemoryBuffer::getMemBufferCopy(bytes, "produced cache entry"));
}

std::string_view IncrementalCacheEntry::payload() const {
	std::string_view entry(contents->getBufferStart(), contents->getBufferSize());
	const size_t offset = header.size() + 65;
	if (entry.size() < offset || !entry.starts_with(header) || entry[offset - 1] != '\n' ||
		stableIdentityDigest(entry.substr(offset)) != entry.substr(header.size(), 64))
		throw std::runtime_error("corrupt cache entry: " + contents->getBufferIdentifier().str());
	return entry.substr(offset);
}

bool IncrementalCacheSnapshots::observe(const std::filesystem::path &path) {
	auto [entry, inserted] = entries.try_emplace(path);
	if (inserted)
		entry->second = IncrementalCacheEntry::open(path);
	return entry->second.has_value();
}

bool IncrementalCacheSnapshots::available(const std::filesystem::path &path) const {
	return entries.at(path).has_value();
}

std::string_view IncrementalCacheSnapshots::payload(const std::filesystem::path &path) const {
	return entries.at(path).value().payload();
}

void IncrementalCacheSnapshots::retainProduced(const std::filesystem::path &path, std::string_view payload) {
	auto &entry = entries[path];
	if (!entry)
		entry = IncrementalCacheEntry::fromPayload(payload);
}
