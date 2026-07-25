#ifndef DYNLEX_FILESYSTEM_TRANSACTION_INTERNAL_H
#define DYNLEX_FILESYSTEM_TRANSACTION_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
	DYNLEX_FILESYSTEM_STAGING_ACTIVE = 1,
	DYNLEX_FILESYSTEM_STAGING_SEALED = 2,
	DYNLEX_FILESYSTEM_STAGING_POISONED = 3,
	DYNLEX_FILESYSTEM_STAGING_COMMITTED = 4,
	DYNLEX_FILESYSTEM_STAGING_CANCELLED = 5,
	DYNLEX_FILESYSTEM_STAGING_CLEANUP_FAILED = 6
};

enum {
	DYNLEX_FILESYSTEM_TEST_FAIL_WRITE = 1,
	DYNLEX_FILESYSTEM_TEST_FAIL_RESTORE_TIMES = 2,
	DYNLEX_FILESYSTEM_TEST_FAIL_RESTORE_MODE = 3,
	DYNLEX_FILESYSTEM_TEST_FAIL_CANCEL_UNLINK = 4,
	DYNLEX_FILESYSTEM_TEST_FAIL_STAGE_SYNC = 5,
	DYNLEX_FILESYSTEM_TEST_FAIL_PREFLIGHT_DIRECTORY_SYNC = 6,
	DYNLEX_FILESYSTEM_TEST_FAIL_COMMIT = 7,
	DYNLEX_FILESYSTEM_TEST_FAIL_POST_COMMIT_CLEANUP = 8,
	DYNLEX_FILESYSTEM_TEST_FAIL_FINAL_DIRECTORY_SYNC = 9
};

int dynlex_filesystem_transactions_supported(void);
int dynlex_filesystem_entry(
	const char *path, size_t path_length, int32_t *kind, int32_t *mode_supported, int64_t *mode,
	int32_t *windows_attributes_supported, int64_t *windows_attributes, int64_t *restorable_windows_attributes,
	int32_t *access_time_supported, int64_t *access_seconds, int32_t *access_nanoseconds, int32_t *modification_time_supported,
	int64_t *modification_seconds, int32_t *modification_nanoseconds, int32_t *creation_time_supported,
	int64_t *creation_seconds, int32_t *creation_nanoseconds, int32_t *identity_supported, char *identity,
	size_t identity_capacity, size_t *identity_length
);
void *dynlex_filesystem_staging_create(const char *target, size_t target_length);
void dynlex_filesystem_staging_retain(void *opaque_staging);
void dynlex_filesystem_staging_release(void *opaque_staging);
size_t dynlex_filesystem_staging_path_length(const void *opaque_staging);
int dynlex_filesystem_staging_copy_path(const void *opaque_staging, char *buffer, size_t capacity);
int dynlex_filesystem_staging_state(const void *opaque_staging);
int dynlex_filesystem_staging_write(void *opaque_staging, const char *contents, size_t length);
int dynlex_filesystem_staging_restore_metadata(
	void *opaque_staging, bool mode_supported, int64_t mode, bool windows_attributes_supported,
	int64_t restorable_windows_attributes, bool access_time_supported, int64_t access_seconds, int32_t access_nanoseconds,
	bool modification_time_supported, int64_t modification_seconds, int32_t modification_nanoseconds,
	bool creation_time_supported, int64_t creation_seconds, int32_t creation_nanoseconds
);
int dynlex_filesystem_staging_cancel(void *opaque_staging, int32_t *cleanup_succeeded);
int dynlex_filesystem_staging_commit(
	void *opaque_staging, bool overwrite, bool require_durability, int32_t *outcome_known, int32_t *committed, int32_t *durable,
	int32_t *cleanup_succeeded
);

#ifdef DYNLEX_FILESYSTEM_TESTING
void dynlex_filesystem_test_fail_after(int32_t operation, size_t progress);
void dynlex_filesystem_test_reset_failures(void);
#endif

#endif
