#include "platformFeatureTest.h"

#include "filesystemRuntimeInternal.h"
#include "filesystemStatPosix.h"
#include "filesystemTransactionInternal.h"
#include "runtimeError.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

typedef struct {
	size_t references;
	int parent_descriptor;
	int staging_descriptor;
	int state;
	bool stage_name_present;
	bool committed_destination;
	bool cleanup_requires_sync;
	char *destination_name;
	char *staging_name;
	char *staging_path;
	size_t staging_path_length;
	dev_t parent_device;
	ino_t parent_inode;
	dev_t staging_device;
	ino_t staging_inode;
	bool initial_destination_exists;
	dev_t initial_destination_device;
	ino_t initial_destination_inode;
} DynlexPosixStaging;

static atomic_uint_fast64_t staging_sequence = 1;

#ifdef DYNLEX_FILESYSTEM_TESTING
static int32_t injected_operation;
static size_t injected_progress;

void dynlex_filesystem_test_fail_after(int32_t operation, size_t progress) {
	injected_operation = operation;
	injected_progress = progress;
}

void dynlex_filesystem_test_reset_failures(void) {
	injected_operation = 0;
	injected_progress = 0;
}

static bool injected_failure(int32_t operation, size_t progress) {
	if (injected_operation != operation || progress < injected_progress)
		return false;
	injected_operation = 0;
	errno = EIO;
	return true;
}

static size_t injected_write_size(size_t progress, size_t requested) {
	if (injected_operation != DYNLEX_FILESYSTEM_TEST_FAIL_WRITE || progress >= injected_progress)
		return requested;
	size_t remaining = injected_progress - progress;
	return remaining < requested ? remaining : requested;
}
#else
static bool injected_failure(int32_t operation, size_t progress) {
	(void)operation;
	(void)progress;
	return false;
}

static size_t injected_write_size(size_t progress, size_t requested) {
	(void)progress;
	return requested;
}
#endif

static bool missing_error(int error_number) { return error_number == ENOENT || error_number == ENOTDIR; }

static bool same_identity(const struct stat *left, const struct stat *right) {
	return left->st_dev == right->st_dev && left->st_ino == right->st_ino;
}

static void write_u64_little_endian(unsigned char *destination, uint64_t value) {
	for (size_t index = 0; index < 8; ++index)
		destination[index] = (unsigned char)(value >> (index * 8));
}

static int validate_timestamp(int32_t nanoseconds) {
	if (nanoseconds >= 0 && nanoseconds < 1000000000)
		return 0;
	dynlex_runtime_set_error("Filesystem timestamp nanoseconds are outside the valid range");
	return -1;
}

int dynlex_filesystem_transactions_supported(void) { return 1; }

int dynlex_filesystem_entry(
	const char *path, size_t path_length, int32_t *kind, int32_t *mode_supported, int64_t *mode,
	int32_t *windows_attributes_supported, int64_t *windows_attributes, int64_t *restorable_windows_attributes,
	int32_t *access_time_supported, int64_t *access_seconds, int32_t *access_nanoseconds, int32_t *modification_time_supported,
	int64_t *modification_seconds, int32_t *modification_nanoseconds, int32_t *creation_time_supported,
	int64_t *creation_seconds, int32_t *creation_nanoseconds, int32_t *identity_supported, char *identity,
	size_t identity_capacity, size_t *identity_length
) {
	dynlex_runtime_clear_error();
	if (kind == NULL || mode_supported == NULL || mode == NULL || windows_attributes_supported == NULL ||
		windows_attributes == NULL || restorable_windows_attributes == NULL || access_time_supported == NULL ||
		access_seconds == NULL || access_nanoseconds == NULL || modification_time_supported == NULL ||
		modification_seconds == NULL || modification_nanoseconds == NULL || creation_time_supported == NULL ||
		creation_seconds == NULL || creation_nanoseconds == NULL || identity_supported == NULL || identity_length == NULL) {
		dynlex_runtime_set_error("Invalid filesystem entry outputs");
		return -1;
	}
	*identity_length = 0;
	char *prepared = dynlex_filesystem_copy_path(path, path_length);
	if (prepared == NULL)
		return -1;
	struct stat attributes;
	if (lstat(prepared, &attributes) != 0) {
		int error_number = errno;
		free(prepared);
		if (missing_error(error_number))
			return 0;
		dynlex_runtime_set_errno_error("Could not inspect filesystem entry", error_number);
		return -1;
	}
	free(prepared);

	*kind = S_ISREG(attributes.st_mode)	  ? DYNLEX_FILESYSTEM_ENTRY_REGULAR_FILE
			: S_ISDIR(attributes.st_mode) ? DYNLEX_FILESYSTEM_ENTRY_DIRECTORY
			: S_ISLNK(attributes.st_mode) ? DYNLEX_FILESYSTEM_ENTRY_SYMBOLIC_LINK
										  : DYNLEX_FILESYSTEM_ENTRY_OTHER;
	*mode_supported = 1;
	*mode = (int64_t)(attributes.st_mode & 07777);
	*windows_attributes_supported = 0;
	*windows_attributes = 0;
	*restorable_windows_attributes = 0;
	*access_time_supported = 1;
	*access_seconds = dynlex_stat_access_seconds(&attributes);
	*access_nanoseconds = dynlex_stat_access_nanoseconds(&attributes);
	*modification_time_supported = 1;
	*modification_seconds = dynlex_stat_modification_seconds(&attributes);
	*modification_nanoseconds = dynlex_stat_modification_nanoseconds(&attributes);
	*creation_time_supported = 0;
	*creation_seconds = 0;
	*creation_nanoseconds = 0;
	*identity_supported = 1;
	if (identity == NULL || identity_capacity < 18) {
		dynlex_runtime_set_error("Filesystem identity buffer is too small");
		return -1;
	}
	identity[0] = 1;
	identity[1] = 1;
	write_u64_little_endian((unsigned char *)identity + 2, (uint64_t)attributes.st_dev);
	write_u64_little_endian((unsigned char *)identity + 10, (uint64_t)attributes.st_ino);
	*identity_length = 18;
	return 1;
}

static void destroy_staging(DynlexPosixStaging *staging) {
	if (staging->staging_descriptor >= 0)
		close(staging->staging_descriptor);
	if (staging->parent_descriptor >= 0)
		close(staging->parent_descriptor);
	free(staging->destination_name);
	free(staging->staging_name);
	free(staging->staging_path);
	free(staging);
}

static int
split_target(const char *target, size_t target_length, char **parent_path, char **destination_name, char **display_prefix) {
	char *prepared = dynlex_filesystem_copy_path(target, target_length);
	if (prepared == NULL)
		return -1;
	char *separator = strrchr(prepared, '/');
	const char *name = separator == NULL ? prepared : separator + 1;
	if (name[0] == '\0' || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
		free(prepared);
		dynlex_runtime_set_error("Filesystem transaction destination must name a file");
		return -1;
	}
	*destination_name = strdup(name);
	if (*destination_name == NULL) {
		free(prepared);
		dynlex_runtime_set_errno_error("Could not allocate filesystem transaction destination", ENOMEM);
		return -1;
	}
	if (separator == NULL) {
		*parent_path = strdup(".");
		*display_prefix = strdup(".");
	} else if (separator == prepared) {
		*parent_path = strdup("/");
		*display_prefix = strdup("/");
	} else {
		*separator = '\0';
		*parent_path = strdup(prepared);
		*display_prefix = strdup(prepared);
	}
	free(prepared);
	if (*parent_path != NULL && *display_prefix != NULL)
		return 0;
	free(*parent_path);
	free(*display_prefix);
	free(*destination_name);
	*parent_path = NULL;
	*display_prefix = NULL;
	*destination_name = NULL;
	dynlex_runtime_set_errno_error("Could not allocate filesystem transaction path", ENOMEM);
	return -1;
}

static int
destination_attributes(int parent_descriptor, const char *name, bool *exists, struct stat *attributes, const char *operation) {
	if (fstatat(parent_descriptor, name, attributes, AT_SYMLINK_NOFOLLOW) == 0) {
		if (!S_ISREG(attributes->st_mode)) {
			dynlex_runtime_set_error("Filesystem transaction destination is not a regular file");
			return -1;
		}
		*exists = true;
		return 0;
	}
	if (errno == ENOENT) {
		*exists = false;
		memset(attributes, 0, sizeof(*attributes));
		return 0;
	}
	dynlex_runtime_set_errno_error(operation, errno);
	return -1;
}

static char *staging_display_path(const char *prefix, const char *name, size_t *length) {
	size_t prefix_length = strlen(prefix);
	size_t name_length = strlen(name);
	bool has_separator = prefix_length > 0 && prefix[prefix_length - 1] == '/';
	if (prefix_length > SIZE_MAX - name_length - (has_separator ? 1 : 2)) {
		dynlex_runtime_set_error("Filesystem staging path is too large");
		return NULL;
	}
	*length = prefix_length + (has_separator ? 0 : 1) + name_length;
	char *result = malloc(*length + 1);
	if (result == NULL) {
		dynlex_runtime_set_errno_error("Could not allocate filesystem staging path", ENOMEM);
		return NULL;
	}
	memcpy(result, prefix, prefix_length);
	size_t offset = prefix_length;
	if (!has_separator)
		result[offset++] = '/';
	memcpy(result + offset, name, name_length + 1);
	return result;
}

static int create_staging_entry(DynlexPosixStaging *staging, const char *display_prefix) {
	struct timespec now;
	if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
		dynlex_runtime_set_errno_error("Could not obtain a filesystem staging nonce", errno);
		return -1;
	}
	for (unsigned int attempt = 0; attempt < 128; ++attempt) {
		uint64_t sequence = atomic_fetch_add_explicit(&staging_sequence, 1, memory_order_relaxed);
		char name[96];
		int length = snprintf(
			name, sizeof(name), ".dynlex-stage-%" PRIx64 "-%" PRIx64 "-%x", (uint64_t)getpid(),
			(uint64_t)now.tv_nsec ^ sequence, attempt
		);
		if (length < 0 || (size_t)length >= sizeof(name)) {
			dynlex_runtime_set_error("Could not format filesystem staging name");
			return -1;
		}
		int descriptor = openat(staging->parent_descriptor, name, O_CREAT | O_EXCL | O_NOFOLLOW | O_RDWR | O_CLOEXEC, 0600);
		if (descriptor < 0) {
			if (errno == EEXIST)
				continue;
			dynlex_runtime_set_errno_error("Could not create filesystem staging file", errno);
			return -1;
		}
		staging->staging_name = strdup(name);
		staging->staging_path = staging_display_path(display_prefix, name, &staging->staging_path_length);
		if (staging->staging_name == NULL || staging->staging_path == NULL) {
			int error_number = errno == 0 ? ENOMEM : errno;
			close(descriptor);
			unlinkat(staging->parent_descriptor, name, 0);
			if (staging->staging_name == NULL)
				dynlex_runtime_set_errno_error("Could not allocate filesystem staging name", error_number);
			return -1;
		}
		staging->staging_descriptor = descriptor;
		staging->stage_name_present = true;
		return 0;
	}
	dynlex_runtime_set_error("Could not create a unique filesystem staging file");
	return -1;
}

static int current_stage_identity(DynlexPosixStaging *staging, struct stat *descriptor_attributes) {
	struct stat path_attributes;
	if (staging->staging_descriptor < 0 || !staging->stage_name_present ||
		fstat(staging->staging_descriptor, descriptor_attributes) != 0 ||
		fstatat(staging->parent_descriptor, staging->staging_name, &path_attributes, AT_SYMLINK_NOFOLLOW) != 0) {
		dynlex_runtime_set_errno_error("Could not recheck filesystem staging identity", errno == 0 ? EIO : errno);
		return -1;
	}
	if (!S_ISREG(descriptor_attributes->st_mode) || !S_ISREG(path_attributes.st_mode) ||
		!same_identity(descriptor_attributes, &path_attributes) || descriptor_attributes->st_dev != staging->staging_device ||
		descriptor_attributes->st_ino != staging->staging_inode) {
		dynlex_runtime_set_error("Filesystem staging identity changed");
		return -1;
	}
	return 0;
}

static int record_stage_identity(DynlexPosixStaging *staging) {
	struct stat descriptor_attributes;
	struct stat path_attributes;
	if (fstat(staging->staging_descriptor, &descriptor_attributes) != 0 ||
		fstatat(staging->parent_descriptor, staging->staging_name, &path_attributes, AT_SYMLINK_NOFOLLOW) != 0) {
		dynlex_runtime_set_errno_error("Could not inspect filesystem staging identity", errno);
		return -1;
	}
	if (!S_ISREG(descriptor_attributes.st_mode) || !S_ISREG(path_attributes.st_mode) ||
		!same_identity(&descriptor_attributes, &path_attributes)) {
		dynlex_runtime_set_error("Created filesystem staging identity is inconsistent");
		return -1;
	}
	staging->staging_device = descriptor_attributes.st_dev;
	staging->staging_inode = descriptor_attributes.st_ino;
	return 0;
}

void *dynlex_filesystem_staging_create(const char *target, size_t target_length) {
	dynlex_runtime_clear_error();
	char *parent_path = NULL;
	char *destination_name = NULL;
	char *display_prefix = NULL;
	if (split_target(target, target_length, &parent_path, &destination_name, &display_prefix) != 0)
		return NULL;
	DynlexPosixStaging *staging = calloc(1, sizeof(*staging));
	if (staging == NULL) {
		free(parent_path);
		free(destination_name);
		free(display_prefix);
		dynlex_runtime_set_errno_error("Could not allocate filesystem staging handle", ENOMEM);
		return NULL;
	}
	staging->parent_descriptor = -1;
	staging->staging_descriptor = -1;
	staging->destination_name = destination_name;
	staging->parent_descriptor = open(parent_path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	free(parent_path);
	if (staging->parent_descriptor < 0) {
		dynlex_runtime_set_errno_error("Could not open filesystem transaction parent directory", errno);
		free(display_prefix);
		destroy_staging(staging);
		return NULL;
	}
	struct stat parent_attributes;
	struct stat destination;
	if (fstat(staging->parent_descriptor, &parent_attributes) != 0 ||
		destination_attributes(
			staging->parent_descriptor, staging->destination_name, &staging->initial_destination_exists, &destination,
			"Could not inspect filesystem transaction destination"
		) != 0 ||
		create_staging_entry(staging, display_prefix) != 0) {
		free(display_prefix);
		destroy_staging(staging);
		return NULL;
	}
	free(display_prefix);
	staging->parent_device = parent_attributes.st_dev;
	staging->parent_inode = parent_attributes.st_ino;
	staging->initial_destination_device = destination.st_dev;
	staging->initial_destination_inode = destination.st_ino;
	if (record_stage_identity(staging) != 0) {
		unlinkat(staging->parent_descriptor, staging->staging_name, 0);
		destroy_staging(staging);
		return NULL;
	}
	staging->state = DYNLEX_FILESYSTEM_STAGING_ACTIVE;
	return staging;
}

void dynlex_filesystem_staging_retain(void *opaque_staging) {
	DynlexPosixStaging *staging = opaque_staging;
	if (staging == NULL)
		return;
	if (staging->references == SIZE_MAX)
		abort();
	staging->references++;
}

static int cleanup_staging(DynlexPosixStaging *staging, bool sync_directory, int32_t failure_operation) {
	if (staging->stage_name_present) {
		struct stat ignored;
		if (current_stage_identity(staging, &ignored) != 0)
			return -1;
		if (injected_failure(failure_operation, 0) || unlinkat(staging->parent_descriptor, staging->staging_name, 0) != 0) {
			dynlex_runtime_set_errno_error("Could not remove filesystem staging file", errno);
			return -1;
		}
		staging->stage_name_present = false;
	}
	if (sync_directory || staging->cleanup_requires_sync) {
		if (injected_failure(DYNLEX_FILESYSTEM_TEST_FAIL_FINAL_DIRECTORY_SYNC, 0) || fsync(staging->parent_descriptor) != 0) {
			staging->cleanup_requires_sync = true;
			dynlex_runtime_set_errno_error("Could not synchronize filesystem transaction directory", errno);
			return -1;
		}
		staging->cleanup_requires_sync = false;
	}
	return 0;
}

void dynlex_filesystem_staging_release(void *opaque_staging) {
	DynlexPosixStaging *staging = opaque_staging;
	if (staging == NULL)
		return;
	if (staging->references == 0)
		abort();
	if (--staging->references != 0)
		return;
	if (staging->state == DYNLEX_FILESYSTEM_STAGING_ACTIVE || staging->state == DYNLEX_FILESYSTEM_STAGING_SEALED ||
		staging->state == DYNLEX_FILESYSTEM_STAGING_POISONED || staging->state == DYNLEX_FILESYSTEM_STAGING_CLEANUP_FAILED)
		(void)cleanup_staging(staging, false, DYNLEX_FILESYSTEM_TEST_FAIL_CANCEL_UNLINK);
	destroy_staging(staging);
}

size_t dynlex_filesystem_staging_path_length(const void *opaque_staging) {
	const DynlexPosixStaging *staging = opaque_staging;
	return staging == NULL ? 0 : staging->staging_path_length;
}

int dynlex_filesystem_staging_copy_path(const void *opaque_staging, char *buffer, size_t capacity) {
	dynlex_runtime_clear_error();
	const DynlexPosixStaging *staging = opaque_staging;
	if (staging == NULL || buffer == NULL || capacity <= staging->staging_path_length) {
		dynlex_runtime_set_error("Filesystem staging path buffer is too small");
		return -1;
	}
	memcpy(buffer, staging->staging_path, staging->staging_path_length + 1);
	return 0;
}

int dynlex_filesystem_staging_state(const void *opaque_staging) {
	const DynlexPosixStaging *staging = opaque_staging;
	return staging == NULL ? 0 : staging->state;
}

int dynlex_filesystem_staging_write(void *opaque_staging, const char *contents, size_t length) {
	dynlex_runtime_clear_error();
	DynlexPosixStaging *staging = opaque_staging;
	if (staging == NULL || (contents == NULL && length > 0)) {
		dynlex_runtime_set_error("Invalid filesystem staging write arguments");
		return -1;
	}
	if (staging->state != DYNLEX_FILESYSTEM_STAGING_ACTIVE) {
		dynlex_runtime_set_error("Filesystem staging file is not active");
		return -1;
	}
	struct stat ignored;
	if (current_stage_identity(staging, &ignored) != 0) {
		staging->state = DYNLEX_FILESYSTEM_STAGING_POISONED;
		return -1;
	}
	size_t written = 0;
	while (written < length) {
		if (injected_failure(DYNLEX_FILESYSTEM_TEST_FAIL_WRITE, written)) {
			dynlex_runtime_set_errno_error("Could not write filesystem staging file", errno);
			staging->state = DYNLEX_FILESYSTEM_STAGING_POISONED;
			return -1;
		}
		size_t requested = injected_write_size(written, length - written);
		ssize_t result = write(staging->staging_descriptor, contents + written, requested);
		if (result < 0 && errno == EINTR)
			continue;
		if (result <= 0) {
			dynlex_runtime_set_errno_error("Could not write filesystem staging file", result == 0 ? EIO : errno);
			staging->state = DYNLEX_FILESYSTEM_STAGING_POISONED;
			return -1;
		}
		written += (size_t)result;
	}
	return 0;
}

int dynlex_filesystem_staging_restore_metadata(
	void *opaque_staging, bool mode_supported, int64_t mode, bool windows_attributes_supported,
	int64_t restorable_windows_attributes, bool access_time_supported, int64_t access_seconds, int32_t access_nanoseconds,
	bool modification_time_supported, int64_t modification_seconds, int32_t modification_nanoseconds,
	bool creation_time_supported, int64_t creation_seconds, int32_t creation_nanoseconds
) {
	(void)windows_attributes_supported;
	(void)restorable_windows_attributes;
	(void)creation_time_supported;
	(void)creation_seconds;
	(void)creation_nanoseconds;
	dynlex_runtime_clear_error();
	DynlexPosixStaging *staging = opaque_staging;
	if (staging == NULL) {
		dynlex_runtime_set_error("Invalid filesystem staging handle");
		return -1;
	}
	if (staging->state != DYNLEX_FILESYSTEM_STAGING_ACTIVE) {
		dynlex_runtime_set_error("Filesystem staging file is not active");
		return -1;
	}
	if (!mode_supported || !access_time_supported || !modification_time_supported) {
		staging->state = DYNLEX_FILESYSTEM_STAGING_POISONED;
		dynlex_runtime_set_error("Required POSIX filesystem metadata is unsupported");
		return -2;
	}
	if (validate_timestamp(access_nanoseconds) != 0 || validate_timestamp(modification_nanoseconds) != 0) {
		staging->state = DYNLEX_FILESYSTEM_STAGING_POISONED;
		return -1;
	}
	struct stat ignored;
	if (current_stage_identity(staging, &ignored) != 0) {
		staging->state = DYNLEX_FILESYSTEM_STAGING_POISONED;
		return -1;
	}
	struct timespec times[2] = {
		{.tv_sec = (time_t)access_seconds, .tv_nsec = access_nanoseconds},
		{.tv_sec = (time_t)modification_seconds, .tv_nsec = modification_nanoseconds}
	};
	if ((int64_t)times[0].tv_sec != access_seconds || (int64_t)times[1].tv_sec != modification_seconds ||
		injected_failure(DYNLEX_FILESYSTEM_TEST_FAIL_RESTORE_TIMES, 0) || futimens(staging->staging_descriptor, times) != 0) {
		dynlex_runtime_set_errno_error("Could not restore filesystem staging timestamps", errno == 0 ? EOVERFLOW : errno);
		staging->state = DYNLEX_FILESYSTEM_STAGING_POISONED;
		return -1;
	}
	if (injected_failure(DYNLEX_FILESYSTEM_TEST_FAIL_RESTORE_MODE, 0) ||
		fchmod(staging->staging_descriptor, (mode_t)(mode & 07777)) != 0) {
		dynlex_runtime_set_errno_error("Could not restore filesystem staging mode", errno);
		staging->state = DYNLEX_FILESYSTEM_STAGING_POISONED;
		return -1;
	}
	staging->state = DYNLEX_FILESYSTEM_STAGING_SEALED;
	return 0;
}

int dynlex_filesystem_staging_cancel(void *opaque_staging, int32_t *cleanup_succeeded) {
	dynlex_runtime_clear_error();
	DynlexPosixStaging *staging = opaque_staging;
	if (staging == NULL || cleanup_succeeded == NULL) {
		dynlex_runtime_set_error("Invalid filesystem staging cancellation arguments");
		return -1;
	}
	*cleanup_succeeded = 0;
	if (staging->state == DYNLEX_FILESYSTEM_STAGING_COMMITTED || staging->state == DYNLEX_FILESYSTEM_STAGING_CANCELLED) {
		dynlex_runtime_set_error("Filesystem staging transaction is already terminal");
		return -1;
	}
	if (cleanup_staging(staging, false, DYNLEX_FILESYSTEM_TEST_FAIL_CANCEL_UNLINK) != 0) {
		staging->state = DYNLEX_FILESYSTEM_STAGING_CLEANUP_FAILED;
		return -1;
	}
	*cleanup_succeeded = 1;
	staging->state = staging->committed_destination ? DYNLEX_FILESYSTEM_STAGING_COMMITTED : DYNLEX_FILESYSTEM_STAGING_CANCELLED;
	return 0;
}

static int precommit_recheck(DynlexPosixStaging *staging, bool overwrite) {
	struct stat parent;
	struct stat stage;
	struct stat destination;
	bool destination_exists = false;
	if (fstat(staging->parent_descriptor, &parent) != 0 || parent.st_dev != staging->parent_device ||
		parent.st_ino != staging->parent_inode) {
		dynlex_runtime_set_error("Filesystem transaction parent identity changed");
		return -1;
	}
	if (current_stage_identity(staging, &stage) != 0)
		return -1;
	if (destination_attributes(
			staging->parent_descriptor, staging->destination_name, &destination_exists, &destination,
			"Could not recheck filesystem transaction destination"
		) != 0)
		return -1;
	if (overwrite && (destination_exists != staging->initial_destination_exists ||
					  (destination_exists && (destination.st_dev != staging->initial_destination_device ||
											  destination.st_ino != staging->initial_destination_inode)))) {
		dynlex_runtime_set_error("Filesystem transaction destination identity changed");
		return -1;
	}
	return 0;
}

static int finish_failed_commit(DynlexPosixStaging *staging, bool sync_directory, int32_t *cleanup_succeeded) {
	if (sync_directory)
		staging->cleanup_requires_sync = true;
	if (cleanup_staging(staging, sync_directory, DYNLEX_FILESYSTEM_TEST_FAIL_CANCEL_UNLINK) != 0) {
		staging->state = DYNLEX_FILESYSTEM_STAGING_CLEANUP_FAILED;
		return -1;
	}
	*cleanup_succeeded = 1;
	staging->state = DYNLEX_FILESYSTEM_STAGING_CANCELLED;
	return -1;
}

int dynlex_filesystem_staging_commit(
	void *opaque_staging, bool overwrite, bool require_durability, int32_t *outcome_known, int32_t *committed, int32_t *durable,
	int32_t *cleanup_succeeded
) {
	dynlex_runtime_clear_error();
	DynlexPosixStaging *staging = opaque_staging;
	if (staging == NULL || outcome_known == NULL || committed == NULL || durable == NULL || cleanup_succeeded == NULL) {
		dynlex_runtime_set_error("Invalid filesystem staging commit arguments");
		return -1;
	}
	*outcome_known = 1;
	*committed = 0;
	*durable = 0;
	*cleanup_succeeded = 0;
	if (staging->state != DYNLEX_FILESYSTEM_STAGING_ACTIVE && staging->state != DYNLEX_FILESYSTEM_STAGING_SEALED) {
		dynlex_runtime_set_error("Filesystem staging transaction cannot be committed in its current state");
		return -1;
	}
	if (precommit_recheck(staging, overwrite) != 0)
		return finish_failed_commit(staging, require_durability, cleanup_succeeded);
	if (require_durability &&
		(injected_failure(DYNLEX_FILESYSTEM_TEST_FAIL_STAGE_SYNC, 0) || fsync(staging->staging_descriptor) != 0)) {
		dynlex_runtime_set_errno_error("Could not synchronize filesystem staging file", errno);
		return finish_failed_commit(staging, true, cleanup_succeeded);
	}
	if (require_durability &&
		(injected_failure(DYNLEX_FILESYSTEM_TEST_FAIL_PREFLIGHT_DIRECTORY_SYNC, 0) || fsync(staging->parent_descriptor) != 0)) {
		dynlex_runtime_set_errno_error("Could not preflight filesystem transaction directory durability", errno);
		return finish_failed_commit(staging, true, cleanup_succeeded);
	}
	if (precommit_recheck(staging, overwrite) != 0)
		return finish_failed_commit(staging, require_durability, cleanup_succeeded);
	if (injected_failure(DYNLEX_FILESYSTEM_TEST_FAIL_COMMIT, 0)) {
		dynlex_runtime_set_errno_error("Could not commit filesystem staging transaction", errno);
		return finish_failed_commit(staging, require_durability, cleanup_succeeded);
	}
	int commit_result =
		overwrite
			? renameat(staging->parent_descriptor, staging->staging_name, staging->parent_descriptor, staging->destination_name)
			: linkat(
				  staging->parent_descriptor, staging->staging_name, staging->parent_descriptor, staging->destination_name, 0
			  );
	if (commit_result != 0) {
		dynlex_runtime_set_errno_error("Could not commit filesystem staging transaction", errno);
		return finish_failed_commit(staging, require_durability, cleanup_succeeded);
	}
	staging->committed_destination = true;
	*committed = 1;
	if (overwrite)
		staging->stage_name_present = false;
	struct stat destination;
	if (fstatat(staging->parent_descriptor, staging->destination_name, &destination, AT_SYMLINK_NOFOLLOW) != 0 ||
		!S_ISREG(destination.st_mode) || destination.st_dev != staging->staging_device ||
		destination.st_ino != staging->staging_inode) {
		dynlex_runtime_set_error("Committed filesystem destination identity does not match the staging file");
		staging->state = DYNLEX_FILESYSTEM_STAGING_CLEANUP_FAILED;
		return -1;
	}
	if (!overwrite) {
		if (injected_failure(DYNLEX_FILESYSTEM_TEST_FAIL_POST_COMMIT_CLEANUP, 0) ||
			unlinkat(staging->parent_descriptor, staging->staging_name, 0) != 0) {
			dynlex_runtime_set_errno_error("Could not remove committed filesystem staging link", errno);
			staging->state = DYNLEX_FILESYSTEM_STAGING_CLEANUP_FAILED;
			return -1;
		}
		staging->stage_name_present = false;
	}
	*cleanup_succeeded = 1;
	if (require_durability) {
		if (injected_failure(DYNLEX_FILESYSTEM_TEST_FAIL_FINAL_DIRECTORY_SYNC, 0) || fsync(staging->parent_descriptor) != 0) {
			staging->cleanup_requires_sync = true;
			*cleanup_succeeded = 0;
			dynlex_runtime_set_errno_error("Could not finalize filesystem transaction durability", errno);
			staging->state = DYNLEX_FILESYSTEM_STAGING_CLEANUP_FAILED;
			return -1;
		}
		*durable = 1;
	}
	staging->state = DYNLEX_FILESYSTEM_STAGING_COMMITTED;
	return 0;
}
