#define _POSIX_C_SOURCE 200809L

#include "filesystemRuntimeInternal.h"
#include "filesystemTransactionInternal.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

typedef struct {
	int32_t kind;
	int32_t mode_supported;
	int64_t mode;
	int32_t windows_attributes_supported;
	int64_t windows_attributes;
	int64_t restorable_windows_attributes;
	int32_t access_time_supported;
	int64_t access_seconds;
	int32_t access_nanoseconds;
	int32_t modification_time_supported;
	int64_t modification_seconds;
	int32_t modification_nanoseconds;
	int32_t creation_time_supported;
	int64_t creation_seconds;
	int32_t creation_nanoseconds;
	int32_t identity_supported;
	char identity[26];
	size_t identity_length;
} Entry;

#define CHECK(condition)                                                                                                       \
	do {                                                                                                                       \
		if (!(condition)) {                                                                                                    \
			fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, __LINE__, #condition);                                    \
			return 1;                                                                                                          \
		}                                                                                                                      \
	} while (0)

static int inspect_entry(const char *path, Entry *entry) {
	memset(entry, 0, sizeof(*entry));
	return dynlex_filesystem_entry(
		path, strlen(path), &entry->kind, &entry->mode_supported, &entry->mode, &entry->windows_attributes_supported,
		&entry->windows_attributes, &entry->restorable_windows_attributes, &entry->access_time_supported,
		&entry->access_seconds, &entry->access_nanoseconds, &entry->modification_time_supported, &entry->modification_seconds,
		&entry->modification_nanoseconds, &entry->creation_time_supported, &entry->creation_seconds,
		&entry->creation_nanoseconds, &entry->identity_supported, entry->identity, sizeof(entry->identity),
		&entry->identity_length
	);
}

static int write_file(const char *path, const char *contents) {
	int descriptor = open(path, O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC, 0600);
	if (descriptor < 0)
		return -1;
	size_t length = strlen(contents);
	ssize_t written = write(descriptor, contents, length);
	int close_result = close(descriptor);
	return written == (ssize_t)length && close_result == 0 ? 0 : -1;
}

static int read_file(const char *path, char *buffer, size_t capacity) {
	int descriptor = open(path, O_RDONLY | O_CLOEXEC);
	if (descriptor < 0)
		return -1;
	ssize_t length = read(descriptor, buffer, capacity - 1);
	int close_result = close(descriptor);
	if (length < 0 || close_result != 0)
		return -1;
	buffer[length] = '\0';
	return (int)length;
}

static int stage_path(void *staging, char *path, size_t capacity) {
	size_t length = dynlex_filesystem_staging_path_length(staging);
	if (length + 1 > capacity)
		return -1;
	return dynlex_filesystem_staging_copy_path(staging, path, capacity);
}

static int restore_snapshot(void *staging, const Entry *entry) {
	return dynlex_filesystem_staging_restore_metadata(
		staging, entry->mode_supported != 0, entry->mode, entry->windows_attributes_supported != 0,
		entry->restorable_windows_attributes, entry->access_time_supported != 0, entry->access_seconds,
		entry->access_nanoseconds, entry->modification_time_supported != 0, entry->modification_seconds,
		entry->modification_nanoseconds, entry->creation_time_supported != 0, entry->creation_seconds,
		entry->creation_nanoseconds
	);
}

static int commit_stage(
	void *staging, int overwrite, int require_durability, int32_t *committed, int32_t *durable, int32_t *cleanup_succeeded
) {
	int32_t outcome_known = 0;
	int result = dynlex_filesystem_staging_commit(
		staging, overwrite != 0, require_durability != 0, &outcome_known, committed, durable, cleanup_succeeded
	);
	if (outcome_known != 1)
		return -99;
	return result;
}

static int cancel_stage(void *staging, int32_t *cleanup_succeeded) {
	return dynlex_filesystem_staging_cancel(staging, cleanup_succeeded);
}

static int directory_has_stage(const char *path) {
	DIR *directory = opendir(path);
	if (directory == NULL)
		return -1;
	int found = 0;
	for (;;) {
		struct dirent *entry = readdir(directory);
		if (entry == NULL)
			break;
		if (strncmp(entry->d_name, ".dynlex-stage-", 14) == 0)
			found = 1;
	}
	int close_result = closedir(directory);
	return close_result == 0 ? found : -1;
}

static int cancel_and_release(void *staging) {
	int32_t cleanup_succeeded = 0;
	int result = cancel_stage(staging, &cleanup_succeeded);
	dynlex_filesystem_staging_retain(staging);
	dynlex_filesystem_staging_release(staging);
	return result == 0 && cleanup_succeeded == 1 ? 0 : -1;
}

int main(void) {
	char root[] = "/tmp/dynlex-filesystem-transaction-XXXXXX";
	CHECK(mkdtemp(root) != NULL);
	char original[512];
	char created[512];
	char durable_path[512];
	char large_path[512];
	char race_path[512];
	char race_backup[512];
	char stage_backup[512];
	char nonregular[512];
	CHECK(snprintf(original, sizeof(original), "%s/original.bin", root) > 0);
	CHECK(snprintf(created, sizeof(created), "%s/created.bin", root) > 0);
	CHECK(snprintf(durable_path, sizeof(durable_path), "%s/durable.bin", root) > 0);
	CHECK(snprintf(large_path, sizeof(large_path), "%s/large.bin", root) > 0);
	CHECK(snprintf(race_path, sizeof(race_path), "%s/race.bin", root) > 0);
	CHECK(snprintf(race_backup, sizeof(race_backup), "%s/race-backup.bin", root) > 0);
	CHECK(snprintf(stage_backup, sizeof(stage_backup), "%s/stage-backup.bin", root) > 0);
	CHECK(snprintf(nonregular, sizeof(nonregular), "%s/directory", root) > 0);
	CHECK(write_file(original, "old") == 0);
	CHECK(chmod(original, 0640) == 0);
	struct timespec exact_times[2] = {
		{.tv_sec = 1712345678, .tv_nsec = 123456789}, {.tv_sec = 1712345680, .tv_nsec = 987654321}
	};
	CHECK(utimensat(AT_FDCWD, original, exact_times, 0) == 0);

	Entry original_entry;
	CHECK(inspect_entry(original, &original_entry) == 1);
	CHECK(original_entry.kind == DYNLEX_FILESYSTEM_ENTRY_REGULAR_FILE);
	CHECK(original_entry.mode_supported == 1 && original_entry.mode == 0640);
	CHECK(original_entry.windows_attributes_supported == 0);
	CHECK(original_entry.access_seconds == exact_times[0].tv_sec);
	CHECK(original_entry.access_nanoseconds == exact_times[0].tv_nsec);
	CHECK(original_entry.modification_seconds == exact_times[1].tv_sec);
	CHECK(original_entry.modification_nanoseconds == exact_times[1].tv_nsec);
	CHECK(original_entry.creation_time_supported == 0);
	CHECK(original_entry.identity_supported == 1 && original_entry.identity_length == 18);
	CHECK((unsigned char)original_entry.identity[0] == 1);
	CHECK((unsigned char)original_entry.identity[1] == 1);

	void *staging = dynlex_filesystem_staging_create(original, strlen(original));
	CHECK(staging != NULL);
	dynlex_filesystem_staging_retain(staging);
	CHECK(dynlex_filesystem_staging_state(staging) == DYNLEX_FILESYSTEM_STAGING_ACTIVE);
	char staging_name[512];
	CHECK(stage_path(staging, staging_name, sizeof(staging_name)) == 0);
	CHECK(dynlex_filesystem_staging_write(staging, "first", 5) == 0);
	CHECK(dynlex_filesystem_staging_write(staging, "second", 6) == 0);
	CHECK(restore_snapshot(staging, &original_entry) == 0);
	CHECK(dynlex_filesystem_staging_state(staging) == DYNLEX_FILESYSTEM_STAGING_SEALED);
	CHECK(dynlex_filesystem_staging_write(staging, "invalid", 7) != 0);
	CHECK(dynlex_filesystem_staging_state(staging) == DYNLEX_FILESYSTEM_STAGING_SEALED);
	int32_t committed = 0;
	int32_t durable = 0;
	int32_t cleanup_succeeded = 0;
	CHECK(commit_stage(staging, 0, 0, &committed, &durable, &cleanup_succeeded) != 0);
	CHECK(committed == 0 && durable == 0 && cleanup_succeeded == 1);
	CHECK(dynlex_filesystem_staging_state(staging) == DYNLEX_FILESYSTEM_STAGING_CANCELLED);
	CHECK(access(staging_name, F_OK) != 0 && errno == ENOENT);
	CHECK(cancel_stage(staging, &cleanup_succeeded) != 0);
	dynlex_filesystem_staging_release(staging);

	staging = dynlex_filesystem_staging_create(original, strlen(original));
	CHECK(staging != NULL);
	dynlex_filesystem_staging_retain(staging);
	CHECK(dynlex_filesystem_staging_write(staging, "first", 5) == 0);
	CHECK(dynlex_filesystem_staging_write(staging, "second", 6) == 0);
	CHECK(restore_snapshot(staging, &original_entry) == 0);
	CHECK(commit_stage(staging, 1, 0, &committed, &durable, &cleanup_succeeded) == 0);
	CHECK(committed == 1 && durable == 0 && cleanup_succeeded == 1);
	CHECK(dynlex_filesystem_staging_state(staging) == DYNLEX_FILESYSTEM_STAGING_COMMITTED);
	Entry replaced_entry;
	CHECK(inspect_entry(original, &replaced_entry) == 1);
	CHECK(replaced_entry.mode == original_entry.mode);
	CHECK(replaced_entry.access_seconds == original_entry.access_seconds);
	CHECK(replaced_entry.access_nanoseconds == original_entry.access_nanoseconds);
	CHECK(replaced_entry.modification_seconds == original_entry.modification_seconds);
	CHECK(replaced_entry.modification_nanoseconds == original_entry.modification_nanoseconds);
	char contents[64];
	CHECK(read_file(original, contents, sizeof(contents)) == 11);
	CHECK(strcmp(contents, "firstsecond") == 0);
	dynlex_filesystem_staging_release(staging);

	staging = dynlex_filesystem_staging_create(created, strlen(created));
	CHECK(staging != NULL);
	dynlex_filesystem_staging_retain(staging);
	CHECK(dynlex_filesystem_staging_write(staging, "created", 7) == 0);
	CHECK(commit_stage(staging, 0, 0, &committed, &durable, &cleanup_succeeded) == 0);
	CHECK(committed == 1 && cleanup_succeeded == 1);
	CHECK(read_file(created, contents, sizeof(contents)) == 7);
	CHECK(strcmp(contents, "created") == 0);
	CHECK(commit_stage(staging, 0, 0, &committed, &durable, &cleanup_succeeded) != 0);
	dynlex_filesystem_staging_release(staging);

	staging = dynlex_filesystem_staging_create(durable_path, strlen(durable_path));
	CHECK(staging != NULL);
	dynlex_filesystem_staging_retain(staging);
	CHECK(dynlex_filesystem_staging_write(staging, "durable", 7) == 0);
	CHECK(commit_stage(staging, 0, 1, &committed, &durable, &cleanup_succeeded) == 0);
	CHECK(committed == 1 && durable == 1 && cleanup_succeeded == 1);
	dynlex_filesystem_staging_release(staging);

	const size_t large_length = 1024 * 1024;
	char *large_contents = malloc(large_length);
	CHECK(large_contents != NULL);
	for (size_t index = 0; index < large_length; ++index)
		large_contents[index] = (char)(index % 251);
	staging = dynlex_filesystem_staging_create(large_path, strlen(large_path));
	CHECK(staging != NULL);
	dynlex_filesystem_staging_retain(staging);
	CHECK(dynlex_filesystem_staging_write(staging, large_contents, large_length / 2) == 0);
	CHECK(dynlex_filesystem_staging_write(staging, large_contents + large_length / 2, large_length - large_length / 2) == 0);
	CHECK(commit_stage(staging, 0, 0, &committed, &durable, &cleanup_succeeded) == 0);
	struct stat large_attributes;
	CHECK(stat(large_path, &large_attributes) == 0);
	CHECK(large_attributes.st_size == (off_t)large_length);
	int large_descriptor = open(large_path, O_RDONLY | O_CLOEXEC);
	CHECK(large_descriptor >= 0);
	for (size_t offset = 0; offset < large_length;) {
		char buffer[4096];
		ssize_t count = read(large_descriptor, buffer, sizeof(buffer));
		CHECK(count > 0);
		CHECK(memcmp(buffer, large_contents + offset, (size_t)count) == 0);
		offset += (size_t)count;
	}
	CHECK(close(large_descriptor) == 0);
	free(large_contents);
	dynlex_filesystem_staging_release(staging);

	staging = dynlex_filesystem_staging_create(race_path, strlen(race_path));
	CHECK(staging != NULL);
	CHECK(cancel_and_release(staging) == 0);

	staging = dynlex_filesystem_staging_create(race_path, strlen(race_path));
	CHECK(staging != NULL);
	dynlex_filesystem_staging_retain(staging);
	dynlex_filesystem_test_fail_after(DYNLEX_FILESYSTEM_TEST_FAIL_WRITE, 2);
	CHECK(dynlex_filesystem_staging_write(staging, "partial", 7) != 0);
	CHECK(dynlex_filesystem_staging_state(staging) == DYNLEX_FILESYSTEM_STAGING_POISONED);
	CHECK(stage_path(staging, staging_name, sizeof(staging_name)) == 0);
	CHECK(read_file(staging_name, contents, sizeof(contents)) == 2);
	CHECK(memcmp(contents, "pa", 2) == 0);
	CHECK(cancel_stage(staging, &cleanup_succeeded) == 0 && cleanup_succeeded == 1);
	dynlex_filesystem_staging_release(staging);

	staging = dynlex_filesystem_staging_create(race_path, strlen(race_path));
	CHECK(staging != NULL);
	dynlex_filesystem_staging_retain(staging);
	dynlex_filesystem_test_fail_after(DYNLEX_FILESYSTEM_TEST_FAIL_RESTORE_TIMES, 0);
	CHECK(restore_snapshot(staging, &original_entry) != 0);
	CHECK(dynlex_filesystem_staging_state(staging) == DYNLEX_FILESYSTEM_STAGING_POISONED);
	CHECK(cancel_stage(staging, &cleanup_succeeded) == 0);
	dynlex_filesystem_staging_release(staging);

	staging = dynlex_filesystem_staging_create(race_path, strlen(race_path));
	CHECK(staging != NULL);
	dynlex_filesystem_staging_retain(staging);
	CHECK(stage_path(staging, staging_name, sizeof(staging_name)) == 0);
	CHECK(rename(staging_name, stage_backup) == 0);
	CHECK(write_file(staging_name, "substitute") == 0);
	CHECK(dynlex_filesystem_staging_write(staging, "blocked", 7) != 0);
	CHECK(dynlex_filesystem_staging_state(staging) == DYNLEX_FILESYSTEM_STAGING_POISONED);
	CHECK(cancel_stage(staging, &cleanup_succeeded) != 0 && cleanup_succeeded == 0);
	CHECK(dynlex_filesystem_staging_state(staging) == DYNLEX_FILESYSTEM_STAGING_CLEANUP_FAILED);
	CHECK(unlink(staging_name) == 0);
	CHECK(rename(stage_backup, staging_name) == 0);
	CHECK(cancel_stage(staging, &cleanup_succeeded) == 0 && cleanup_succeeded == 1);
	dynlex_filesystem_staging_release(staging);

	const int32_t precommit_failures[] = {
		DYNLEX_FILESYSTEM_TEST_FAIL_STAGE_SYNC, DYNLEX_FILESYSTEM_TEST_FAIL_PREFLIGHT_DIRECTORY_SYNC,
		DYNLEX_FILESYSTEM_TEST_FAIL_COMMIT
	};
	for (size_t index = 0; index < sizeof(precommit_failures) / sizeof(precommit_failures[0]); ++index) {
		staging = dynlex_filesystem_staging_create(race_path, strlen(race_path));
		CHECK(staging != NULL);
		dynlex_filesystem_staging_retain(staging);
		CHECK(dynlex_filesystem_staging_write(staging, "blocked", 7) == 0);
		dynlex_filesystem_test_fail_after(precommit_failures[index], 0);
		bool require_durability = precommit_failures[index] != DYNLEX_FILESYSTEM_TEST_FAIL_COMMIT;
		CHECK(commit_stage(staging, 0, require_durability, &committed, &durable, &cleanup_succeeded) != 0);
		CHECK(committed == 0 && durable == 0 && cleanup_succeeded == 1);
		CHECK(dynlex_filesystem_staging_state(staging) == DYNLEX_FILESYSTEM_STAGING_CANCELLED);
		dynlex_filesystem_staging_release(staging);
	}

	staging = dynlex_filesystem_staging_create(race_path, strlen(race_path));
	CHECK(staging != NULL);
	dynlex_filesystem_staging_retain(staging);
	dynlex_filesystem_test_fail_after(DYNLEX_FILESYSTEM_TEST_FAIL_RESTORE_MODE, 0);
	CHECK(restore_snapshot(staging, &original_entry) != 0);
	CHECK(dynlex_filesystem_staging_state(staging) == DYNLEX_FILESYSTEM_STAGING_POISONED);
	struct stat staging_attributes;
	CHECK(stage_path(staging, staging_name, sizeof(staging_name)) == 0);
	CHECK(stat(staging_name, &staging_attributes) == 0);
	CHECK((staging_attributes.st_mode & 07777) == 0600);
	CHECK(cancel_stage(staging, &cleanup_succeeded) == 0);
	dynlex_filesystem_staging_release(staging);

	staging = dynlex_filesystem_staging_create(race_path, strlen(race_path));
	CHECK(staging != NULL);
	dynlex_filesystem_staging_retain(staging);
	CHECK(
		dynlex_filesystem_staging_restore_metadata(
			staging, false, 0, false, 0, true, original_entry.access_seconds, original_entry.access_nanoseconds, true,
			original_entry.modification_seconds, original_entry.modification_nanoseconds, false, 0, 0
		) == -2
	);
	CHECK(dynlex_filesystem_staging_state(staging) == DYNLEX_FILESYSTEM_STAGING_POISONED);
	CHECK(cancel_stage(staging, &cleanup_succeeded) == 0);
	dynlex_filesystem_staging_release(staging);

	staging = dynlex_filesystem_staging_create(race_path, strlen(race_path));
	CHECK(staging != NULL);
	dynlex_filesystem_staging_retain(staging);
	dynlex_filesystem_test_fail_after(DYNLEX_FILESYSTEM_TEST_FAIL_CANCEL_UNLINK, 0);
	CHECK(cancel_stage(staging, &cleanup_succeeded) != 0 && cleanup_succeeded == 0);
	CHECK(dynlex_filesystem_staging_state(staging) == DYNLEX_FILESYSTEM_STAGING_CLEANUP_FAILED);
	dynlex_filesystem_test_reset_failures();
	CHECK(cancel_stage(staging, &cleanup_succeeded) == 0 && cleanup_succeeded == 1);
	CHECK(dynlex_filesystem_staging_state(staging) == DYNLEX_FILESYSTEM_STAGING_CANCELLED);
	dynlex_filesystem_staging_release(staging);

	unlink(race_path);
	staging = dynlex_filesystem_staging_create(race_path, strlen(race_path));
	CHECK(staging != NULL);
	dynlex_filesystem_staging_retain(staging);
	CHECK(dynlex_filesystem_staging_write(staging, "linked", 6) == 0);
	CHECK(stage_path(staging, staging_name, sizeof(staging_name)) == 0);
	dynlex_filesystem_test_fail_after(DYNLEX_FILESYSTEM_TEST_FAIL_POST_COMMIT_CLEANUP, 0);
	CHECK(commit_stage(staging, 0, 0, &committed, &durable, &cleanup_succeeded) != 0);
	CHECK(committed == 1 && cleanup_succeeded == 0);
	CHECK(dynlex_filesystem_staging_state(staging) == DYNLEX_FILESYSTEM_STAGING_CLEANUP_FAILED);
	CHECK(access(staging_name, F_OK) == 0 && access(race_path, F_OK) == 0);
	dynlex_filesystem_test_reset_failures();
	CHECK(cancel_stage(staging, &cleanup_succeeded) == 0 && cleanup_succeeded == 1);
	CHECK(dynlex_filesystem_staging_state(staging) == DYNLEX_FILESYSTEM_STAGING_COMMITTED);
	CHECK(access(staging_name, F_OK) != 0 && errno == ENOENT);
	dynlex_filesystem_staging_release(staging);

	staging = dynlex_filesystem_staging_create(race_path, strlen(race_path));
	CHECK(staging != NULL);
	dynlex_filesystem_staging_retain(staging);
	CHECK(dynlex_filesystem_staging_write(staging, "synced", 6) == 0);
	dynlex_filesystem_test_fail_after(DYNLEX_FILESYSTEM_TEST_FAIL_FINAL_DIRECTORY_SYNC, 0);
	CHECK(commit_stage(staging, 1, 1, &committed, &durable, &cleanup_succeeded) != 0);
	CHECK(committed == 1 && durable == 0 && cleanup_succeeded == 0);
	CHECK(dynlex_filesystem_staging_state(staging) == DYNLEX_FILESYSTEM_STAGING_CLEANUP_FAILED);
	dynlex_filesystem_test_reset_failures();
	CHECK(cancel_stage(staging, &cleanup_succeeded) == 0 && cleanup_succeeded == 1);
	CHECK(dynlex_filesystem_staging_state(staging) == DYNLEX_FILESYSTEM_STAGING_COMMITTED);
	dynlex_filesystem_staging_release(staging);

	CHECK(write_file(race_path, "first") == 0);
	staging = dynlex_filesystem_staging_create(race_path, strlen(race_path));
	CHECK(staging != NULL);
	dynlex_filesystem_staging_retain(staging);
	CHECK(rename(race_path, race_backup) == 0);
	CHECK(write_file(race_path, "replacement") == 0);
	CHECK(commit_stage(staging, 1, 0, &committed, &durable, &cleanup_succeeded) != 0);
	CHECK(committed == 0 && cleanup_succeeded == 1);
	CHECK(dynlex_filesystem_staging_state(staging) == DYNLEX_FILESYSTEM_STAGING_CANCELLED);
	CHECK(read_file(race_path, contents, sizeof(contents)) == 11);
	CHECK(strcmp(contents, "replacement") == 0);
	dynlex_filesystem_staging_release(staging);
	CHECK(unlink(race_backup) == 0);

	CHECK(mkdir(nonregular, 0700) == 0);
	CHECK(dynlex_filesystem_staging_create(nonregular, strlen(nonregular)) == NULL);
	CHECK(directory_has_stage(root) == 0);

	unlink(original);
	unlink(created);
	unlink(durable_path);
	unlink(large_path);
	unlink(race_path);
	rmdir(nonregular);
	CHECK(rmdir(root) == 0);
	return 0;
}
