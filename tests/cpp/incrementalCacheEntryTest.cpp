#include "incrementalCacheEntry.h"
#include "stableIdentity.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Process.h"
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

void expect(bool condition, const char *message) {
	if (!condition) {
		std::cerr << message << '\n';
		std::exit(1);
	}
}

std::string utf8Path(const fs::path &path) {
	const std::u8string bytes = path.u8string();
	return std::string(reinterpret_cast<const char *>(bytes.data()), bytes.size());
}

void writeEntry(const fs::path &path, const std::string &payload) {
	std::ofstream output(path, std::ios::binary);
	output << IncrementalCacheEntry::header << stableIdentityDigest(payload) << '\n';
	output.write(payload.data(), payload.size());
	output.close();
	expect(bool(output), "Could not write the cache fixture");
}

void verifyCacheBudget(const fs::path &directory) {
	const fs::path filler = directory / ("object-" + std::string(64, 'a') + ".entry");
	const fs::path entry = directory / ("program-" + std::string(64, 'b') + ".entry");
	{
		IncrementalCacheDirectory cache(directory);
		int descriptor;
		expect(!llvm::sys::fs::openFileForWrite(utf8Path(filler), descriptor), "Could not create a sparse cache fixture");
		auto size = 512 * 1024 * 1024 - IncrementalCacheEntry::header.size() - 66;
		expect(!llvm::sys::fs::resize_file_sparse(descriptor, size), "Could not size a sparse cache fixture");
		expect(!llvm::sys::Process::SafelyCloseFileDescriptor(descriptor), "Could not close a sparse cache fixture");
		fs::last_write_time(filler, fs::file_time_type::clock::now() - std::chrono::hours(1));
		expect(cache.trim().entriesScanned == 1, "Initial budget missed an existing entry");
		cache.publish(entry, "a");
		expect(cache.trim().entriesScanned == 0, "Tracking a published entry scanned the directory");
		cache.publish(entry, "b");
		expect(cache.trim().entriesScanned == 0, "Replacing an equal-sized entry increased the budget charge");
		cache.remove(entry);
		cache.publish(entry, "c");
		expect(cache.trim().entriesScanned == 0, "Removing an entry left a budget charge");
		cache.publish(entry, "dd");
		const auto work = cache.trim();
		expect(
			work.entriesScanned == 2 && work.entriesRemoved == 1 && !fs::exists(filler),
			"Growing an entry over the budget did not evict the oldest entry"
		);
		expect(IncrementalCacheEntry::open(entry)->payload() == "dd", "Eviction removed the newly published payload");
	}
	// An interrupted checkpoint is discarded without changing any entry.
	std::ofstream(directory / "publication" / "budget", std::ios::trunc).close();
	IncrementalCacheDirectory recovered(directory);
	expect(recovered.trim().entriesScanned == 1, "Interrupted accounting did not rescan persisted entries");
	expect(recovered.trim().entriesScanned == 0, "Recovered accounting did not persist");
}

int verifyEntries(int argc, char **argv) {
	expect(argc == 2, "Pass a private test directory");
	const fs::path directory = argv[1];
	const fs::path path = directory / fs::path(u8"retained payload 🌽.entry");
	expect(!IncrementalCacheEntry::open(path), "An evicted entry must be a cache miss");
	for (const std::string &payload : {std::string("a\0b", 3), std::string(128 * 1024, 'x')}) {
		writeEntry(path, payload);
		auto retained = IncrementalCacheEntry::open(path);
		IncrementalCacheSnapshots snapshots;
		expect(snapshots.observe(path), "The snapshot set missed an existing entry");
		expect(bool(retained), "The fixture was not opened");
		IncrementalCacheEntry::evict(path);
		expect(retained->payload() == payload, "Eviction invalidated a retained payload");
		expect(snapshots.available(path) && snapshots.payload(path) == payload,
			   "The snapshot set reopened an evicted pathname");
		expect(!IncrementalCacheEntry::open(path), "A new lookup reused an evicted pathname");
		IncrementalCacheSnapshots missing;
		expect(!missing.observe(path) && !missing.available(path), "An initially absent entry must remain a cache miss");
		missing.retainProduced(path, payload);
		expect(missing.available(path) && missing.payload(path) == payload,
			   "A newly produced payload still depended on a file's lifetime");
		writeEntry(path, "new entry");
		expect(IncrementalCacheEntry::open(path)->payload() == "new entry", "Eviction kept the name reserved");
		expect(retained->payload() == payload, "Recreating the pathname changed the old mapping");
		IncrementalCacheEntry::evict(path);
	}
	{
		IncrementalCacheSnapshots produced;
		std::string payload = "owned bytes";
		produced.retainProduced(path, payload);
		payload.assign("overwritten");
		expect(produced.payload(path) == "owned bytes", "The produced snapshot borrowed its producer's buffer");
	}
	writeEntry(path, "first");
	auto first = IncrementalCacheEntry::open(path);
	IncrementalCacheSnapshots snapshots;
	(void)snapshots.observe(path);
	const fs::path replacement = directory / "replacement.entry";
	writeEntry(replacement, "second");
	expect(!llvm::sys::fs::rename(utf8Path(replacement), utf8Path(path)), "Could not atomically replace an entry");
	auto second = IncrementalCacheEntry::open(path);
	expect(first->payload() == "first", "Replacement changed an existing snapshot");
	expect(second->payload() == "second", "A fresh snapshot missed the replacement");
	(void)snapshots.observe(path);
	IncrementalCacheSnapshots fresh;
	(void)fresh.observe(path);
	expect(snapshots.payload(path) == "first" && fresh.payload(path) == "second",
		   "Atomic replacement changed the snapshot set's observed identity");
	IncrementalCacheEntry::evict(path);
	first.reset();
	second.reset();
	writeEntry(path, "valid");
	{
		std::ofstream output(path, std::ios::app | std::ios::binary);
		output << "corrupted";
	}
	bool rejected = false;
	try {
		IncrementalCacheEntry::open(path)->payload();
	} catch (const std::runtime_error &error) {
		rejected = std::string(error.what()).starts_with("corrupt cache entry:");
	}
	expect(rejected, "Retained entries must still reject checksum corruption");
	fs::resize_file(path, IncrementalCacheEntry::maximumBytes + 1);
	rejected = false;
	try {
		IncrementalCacheEntry::open(path);
	} catch (const std::runtime_error &error) {
		rejected = std::string(error.what()).starts_with("cache entry exceeds the size limit:");
	}
	expect(rejected, "An oversized retained entry bypassed the size limit");
	fs::remove(path);
	verifyCacheBudget(directory / "accounting");
	std::cout << "Retained cache payloads survive eviction and atomic replacement; integrity checks remain explicit.\n";
	return 0;
}

int main(int argc, char **argv) {
	try {
		return verifyEntries(argc, argv);
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
