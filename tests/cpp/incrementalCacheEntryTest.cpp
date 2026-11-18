#include "incrementalCacheEntry.h"
#include "stableIdentity.h"
#include "llvm/Support/FileSystem.h"
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

int main(int argc, char **argv) {
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
		expect(!llvm::sys::fs::remove(utf8Path(path)), "Could not evict the retained file");
		expect(retained->payload() == payload, "Eviction invalidated a retained payload");
		expect(snapshots.available(path) && snapshots.payload(path) == payload,
			   "The snapshot set reopened an evicted pathname");
		expect(!IncrementalCacheEntry::open(path), "A new lookup reused an evicted pathname");
		IncrementalCacheSnapshots missing;
		expect(!missing.observe(path) && !missing.available(path), "An initially absent entry must remain a cache miss");
		missing.retainProduced(path, payload);
		expect(missing.available(path) && missing.payload(path) == payload,
			   "A newly produced payload still depended on a file's lifetime");
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
	expect(!llvm::sys::fs::remove(utf8Path(path)), "Could not remove the replaced entry");
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
	std::cout << "Retained cache payloads survive eviction and atomic replacement; integrity checks remain explicit.\n";
}
