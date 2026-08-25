#include "filesystemRuntimeInternal.h"
#include "filesystemTransactionInternal.h"
#include "runtimeError.h"

#include <errno.h>
#include <limits.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>

#define DYNLEX_FILE_RENAME_INFO_CLASS ((FILE_INFO_BY_HANDLE_CLASS)3)
#define DYNLEX_FILE_RENAME_INFO_EX_CLASS ((FILE_INFO_BY_HANDLE_CLASS)22)
#define DYNLEX_FILE_DISPOSITION_INFO_EX_CLASS ((FILE_INFO_BY_HANDLE_CLASS)21)
#define DYNLEX_FILE_RENAME_REPLACE_IF_EXISTS 0x00000001UL
#define DYNLEX_FILE_RENAME_POSIX_SEMANTICS 0x00000002UL
#define DYNLEX_FILE_DISPOSITION_DELETE 0x00000001UL
#define DYNLEX_FILE_DISPOSITION_POSIX_SEMANTICS 0x00000002UL
#define DYNLEX_FILE_DISPOSITION_IGNORE_READONLY_ATTRIBUTE 0x00000010UL

typedef struct {
	DWORD flags;
	HANDLE root_directory;
	DWORD file_name_length;
	WCHAR file_name[1];
} DynlexFileRenameInfo;

typedef struct {
	DWORD flags;
} DynlexFileDispositionInfoEx;

typedef struct {
	size_t references;
	HANDLE parent;
	HANDLE staging;
	int state;
	bool stage_name_present;
	bool committed_destination;
	wchar_t *parent_path;
	wchar_t *destination_name;
	wchar_t *destination_path;
	wchar_t *staging_name;
	wchar_t *staging_path_wide;
	char *staging_path;
	size_t staging_path_length;
	FILE_ID_INFO parent_identity;
	FILE_ID_INFO staging_identity;
	bool initial_destination_exists;
	FILE_ID_INFO initial_destination_identity;
} DynlexWindowsStaging;

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
	SetLastError(ERROR_WRITE_FAULT);
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

static bool missing_error(DWORD error_number) {
	return error_number == ERROR_FILE_NOT_FOUND || error_number == ERROR_PATH_NOT_FOUND || error_number == ERROR_INVALID_NAME;
}

static bool same_identity(const FILE_ID_INFO *left, const FILE_ID_INFO *right) {
	return left->VolumeSerialNumber == right->VolumeSerialNumber &&
		   memcmp(left->FileId.Identifier, right->FileId.Identifier, sizeof(left->FileId.Identifier)) == 0;
}

static void write_u64_little_endian(unsigned char *destination, uint64_t value) {
	for (size_t index = 0; index < 8; ++index)
		destination[index] = (unsigned char)(value >> (index * 8));
}

static HANDLE open_attributes(const wchar_t *path, DWORD extra_access) {
	return CreateFileW(
		path, FILE_READ_ATTRIBUTES | extra_access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
		FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, NULL
	);
}

static int regular_file_identity(HANDLE handle, FILE_ID_INFO *identity, const char *operation) {
	FILE_ATTRIBUTE_TAG_INFO tag;
	if (!GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &tag, (DWORD)sizeof(tag)) ||
		!GetFileInformationByHandleEx(handle, FileIdInfo, identity, (DWORD)sizeof(*identity))) {
		dynlex_runtime_set_windows_error(operation, GetLastError());
		return -1;
	}
	if ((tag.FileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_DEVICE | FILE_ATTRIBUTE_REPARSE_POINT)) != 0) {
		dynlex_runtime_set_error("Filesystem transaction destination is not a regular file");
		return -1;
	}
	return 0;
}

static int path_identity(const wchar_t *path, bool *exists, FILE_ID_INFO *identity, DWORD extra_access, const char *operation) {
	HANDLE handle = open_attributes(path, extra_access);
	if (handle == INVALID_HANDLE_VALUE) {
		DWORD error_number = GetLastError();
		if (missing_error(error_number)) {
			*exists = false;
			memset(identity, 0, sizeof(*identity));
			return 0;
		}
		dynlex_runtime_set_windows_error(operation, error_number);
		return -1;
	}
	*exists = true;
	int result = regular_file_identity(handle, identity, operation);
	CloseHandle(handle);
	return result;
}

static int filetime_parts(FILETIME time, int64_t *seconds, int32_t *nanoseconds) {
	ULARGE_INTEGER ticks;
	ticks.LowPart = time.dwLowDateTime;
	ticks.HighPart = time.dwHighDateTime;
	uint64_t whole_seconds = ticks.QuadPart / UINT64_C(10000000);
	uint64_t remainder = ticks.QuadPart % UINT64_C(10000000);
	if (whole_seconds > (uint64_t)INT64_MAX) {
		dynlex_runtime_set_error("Windows filesystem timestamp is outside the supported range");
		return -1;
	}
	*seconds = (int64_t)whole_seconds - INT64_C(11644473600);
	*nanoseconds = (int32_t)(remainder * 100);
	return 0;
}

static int filetime_value(int64_t seconds, int32_t nanoseconds, FILETIME *time) {
	if (nanoseconds < 0 || nanoseconds >= 1000000000 || nanoseconds % 100 != 0 || seconds < -INT64_C(11644473600)) {
		dynlex_runtime_set_error("Filesystem timestamp cannot be represented exactly by Windows");
		return -1;
	}
	uint64_t epoch_seconds = (uint64_t)(seconds + INT64_C(11644473600));
	if (epoch_seconds > (UINT64_MAX - (uint64_t)nanoseconds / 100) / UINT64_C(10000000)) {
		dynlex_runtime_set_error("Filesystem timestamp cannot be represented exactly by Windows");
		return -1;
	}
	ULARGE_INTEGER ticks;
	ticks.QuadPart = epoch_seconds * UINT64_C(10000000) + (uint64_t)nanoseconds / 100;
	time->dwLowDateTime = ticks.LowPart;
	time->dwHighDateTime = ticks.HighPart;
	return 0;
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
	wchar_t *prepared = dynlex_platform_filesystem_wide_path(path, path_length);
	if (prepared == NULL)
		return -1;
	HANDLE handle = open_attributes(prepared, 0);
	DWORD error_number = GetLastError();
	free(prepared);
	if (handle == INVALID_HANDLE_VALUE) {
		if (missing_error(error_number))
			return 0;
		dynlex_runtime_set_windows_error("Could not inspect filesystem entry", error_number);
		return -1;
	}
	FILE_ATTRIBUTE_TAG_INFO tag;
	FILE_BASIC_INFO basic;
	FILE_ID_INFO file_identity;
	if (!GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &tag, (DWORD)sizeof(tag)) ||
		!GetFileInformationByHandleEx(handle, FileBasicInfo, &basic, (DWORD)sizeof(basic)) ||
		!GetFileInformationByHandleEx(handle, FileIdInfo, &file_identity, (DWORD)sizeof(file_identity))) {
		error_number = GetLastError();
		CloseHandle(handle);
		dynlex_runtime_set_windows_error("Could not inspect filesystem entry", error_number);
		return -1;
	}
	CloseHandle(handle);
	bool reparse = (tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
	*kind = reparse												   ? DYNLEX_FILESYSTEM_ENTRY_SYMBOLIC_LINK
			: (tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ? DYNLEX_FILESYSTEM_ENTRY_DIRECTORY
			: (tag.FileAttributes & FILE_ATTRIBUTE_DEVICE) != 0	   ? DYNLEX_FILESYSTEM_ENTRY_OTHER
																   : DYNLEX_FILESYSTEM_ENTRY_REGULAR_FILE;
	*mode_supported = 0;
	*mode = 0;
	*windows_attributes_supported = 1;
	*windows_attributes = (int64_t)tag.FileAttributes;
	DWORD restorable_mask = FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_ARCHIVE |
							FILE_ATTRIBUTE_NOT_CONTENT_INDEXED;
	*restorable_windows_attributes = (int64_t)(tag.FileAttributes & restorable_mask);
	*access_time_supported = 1;
	*modification_time_supported = 1;
	*creation_time_supported = 1;
	FILETIME access_time = *(FILETIME *)&basic.LastAccessTime;
	FILETIME modification_time = *(FILETIME *)&basic.LastWriteTime;
	FILETIME creation_time = *(FILETIME *)&basic.CreationTime;
	if (filetime_parts(access_time, access_seconds, access_nanoseconds) != 0 ||
		filetime_parts(modification_time, modification_seconds, modification_nanoseconds) != 0 ||
		filetime_parts(creation_time, creation_seconds, creation_nanoseconds) != 0)
		return -1;
	*identity_supported = 1;
	if (identity == NULL || identity_capacity < 26) {
		dynlex_runtime_set_error("Filesystem identity buffer is too small");
		return -1;
	}
	identity[0] = 1;
	identity[1] = 2;
	write_u64_little_endian((unsigned char *)identity + 2, file_identity.VolumeSerialNumber);
	memcpy(identity + 10, file_identity.FileId.Identifier, sizeof(file_identity.FileId.Identifier));
	*identity_length = 26;
	return 1;
}

static wchar_t *duplicate_wide_range(const wchar_t *text, size_t length) {
	if (length >= SIZE_MAX / sizeof(wchar_t))
		return NULL;
	wchar_t *copy = malloc((length + 1) * sizeof(wchar_t));
	if (copy == NULL)
		return NULL;
	memcpy(copy, text, length * sizeof(wchar_t));
	copy[length] = L'\0';
	return copy;
}

static wchar_t *joined_path(const wchar_t *parent, const wchar_t *name) {
	size_t parent_length = wcslen(parent);
	size_t name_length = wcslen(name);
	bool separated = parent_length > 0 && (parent[parent_length - 1] == L'/' || parent[parent_length - 1] == L'\\');
	if (parent_length > SIZE_MAX - name_length - (separated ? 1 : 2) ||
		parent_length + name_length + (separated ? 1 : 2) > SIZE_MAX / sizeof(wchar_t))
		return NULL;
	size_t length = parent_length + (separated ? 0 : 1) + name_length;
	wchar_t *result = malloc((length + 1) * sizeof(wchar_t));
	if (result == NULL)
		return NULL;
	memcpy(result, parent, parent_length * sizeof(wchar_t));
	size_t offset = parent_length;
	if (!separated)
		result[offset++] = L'\\';
	memcpy(result + offset, name, (name_length + 1) * sizeof(wchar_t));
	return result;
}

static int split_target(
	const char *target, size_t target_length, wchar_t **parent_path, wchar_t **destination_name, wchar_t **destination_path
) {
	wchar_t *prepared = dynlex_platform_filesystem_wide_path(target, target_length);
	if (prepared == NULL)
		return -1;
	wchar_t *separator = NULL;
	for (wchar_t *cursor = prepared; *cursor != L'\0'; ++cursor) {
		if (*cursor == L'/' || *cursor == L'\\')
			separator = cursor;
	}
	const wchar_t *name = separator == NULL ? prepared : separator + 1;
	if (name[0] == L'\0' || wcscmp(name, L".") == 0 || wcscmp(name, L"..") == 0) {
		free(prepared);
		dynlex_runtime_set_error("Filesystem transaction destination must name a file");
		return -1;
	}
	*destination_name = _wcsdup(name);
	if (separator == NULL)
		*parent_path = _wcsdup(L".");
	else {
		size_t parent_length = separator == prepared ? 1 : (size_t)(separator - prepared);
		*parent_path = duplicate_wide_range(prepared, parent_length);
	}
	*destination_path = prepared;
	if (*destination_name != NULL && *parent_path != NULL)
		return 0;
	free(*destination_name);
	free(*parent_path);
	free(prepared);
	*destination_name = NULL;
	*parent_path = NULL;
	*destination_path = NULL;
	dynlex_runtime_set_errno_error("Could not allocate filesystem transaction path", ENOMEM);
	return -1;
}

static void destroy_staging(DynlexWindowsStaging *staging) {
	if (staging->staging != INVALID_HANDLE_VALUE)
		CloseHandle(staging->staging);
	if (staging->parent != INVALID_HANDLE_VALUE)
		CloseHandle(staging->parent);
	free(staging->parent_path);
	free(staging->destination_name);
	free(staging->destination_path);
	free(staging->staging_name);
	free(staging->staging_path_wide);
	free(staging->staging_path);
	free(staging);
}

static int create_stage(DynlexWindowsStaging *staging) {
	for (unsigned int attempt = 0; attempt < 128; ++attempt) {
		uint64_t sequence = atomic_fetch_add_explicit(&staging_sequence, 1, memory_order_relaxed);
		wchar_t name[96];
		int length = _snwprintf_s(
			name, _countof(name), _TRUNCATE, L".dynlex-stage-%llx-%llx-%x", (unsigned long long)GetCurrentProcessId(),
			(unsigned long long)sequence, attempt
		);
		if (length < 0) {
			dynlex_runtime_set_error("Could not format filesystem staging name");
			return -1;
		}
		wchar_t *path = joined_path(staging->parent_path, name);
		wchar_t *stored_name = _wcsdup(name);
		if (path == NULL || stored_name == NULL) {
			free(path);
			free(stored_name);
			dynlex_runtime_set_errno_error("Could not allocate filesystem staging path", ENOMEM);
			return -1;
		}
		HANDLE handle = CreateFileW(
			path, GENERIC_WRITE | FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES | DELETE | SYNCHRONIZE,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, CREATE_NEW,
			FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, NULL
		);
		if (handle == INVALID_HANDLE_VALUE) {
			DWORD error_number = GetLastError();
			free(path);
			free(stored_name);
			if (error_number == ERROR_FILE_EXISTS || error_number == ERROR_ALREADY_EXISTS)
				continue;
			dynlex_runtime_set_windows_error("Could not create filesystem staging file", error_number);
			return -1;
		}
		staging->staging = handle;
		staging->staging_name = stored_name;
		staging->staging_path_wide = path;
		staging->staging_path = dynlex_platform_filesystem_utf8_text(path, &staging->staging_path_length);
		if (staging->staging_path == NULL) {
			FILE_DISPOSITION_INFO disposition = {.DeleteFile = TRUE};
			SetFileInformationByHandle(handle, FileDispositionInfo, &disposition, (DWORD)sizeof(disposition));
			return -1;
		}
		staging->stage_name_present = true;
		return 0;
	}
	dynlex_runtime_set_error("Could not create a unique filesystem staging file");
	return -1;
}

static int current_stage_identity(DynlexWindowsStaging *staging) {
	FILE_ID_INFO descriptor_identity;
	if (staging->staging == INVALID_HANDLE_VALUE ||
		regular_file_identity(staging->staging, &descriptor_identity, "Could not recheck filesystem staging identity") != 0)
		return -1;
	bool exists = false;
	FILE_ID_INFO path_file_identity;
	if (path_identity(
			staging->staging_path_wide, &exists, &path_file_identity, 0, "Could not recheck filesystem staging identity"
		) != 0)
		return -1;
	if (!exists || !same_identity(&descriptor_identity, &staging->staging_identity) ||
		!same_identity(&descriptor_identity, &path_file_identity)) {
		dynlex_runtime_set_error("Filesystem staging identity changed");
		return -1;
	}
	return 0;
}

void *dynlex_filesystem_staging_create(const char *target, size_t target_length) {
	dynlex_runtime_clear_error();
	DynlexWindowsStaging *staging = calloc(1, sizeof(*staging));
	if (staging == NULL) {
		dynlex_runtime_set_errno_error("Could not allocate filesystem staging handle", ENOMEM);
		return NULL;
	}
	staging->parent = INVALID_HANDLE_VALUE;
	staging->staging = INVALID_HANDLE_VALUE;
	if (split_target(target, target_length, &staging->parent_path, &staging->destination_name, &staging->destination_path) !=
		0) {
		destroy_staging(staging);
		return NULL;
	}
	staging->parent = CreateFileW(
		staging->parent_path, FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
		FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL
	);
	if (staging->parent == INVALID_HANDLE_VALUE) {
		dynlex_runtime_set_windows_error("Could not open filesystem transaction parent directory", GetLastError());
		destroy_staging(staging);
		return NULL;
	}
	if (!GetFileInformationByHandleEx(
			staging->parent, FileIdInfo, &staging->parent_identity, (DWORD)sizeof(staging->parent_identity)
		) ||
		path_identity(
			staging->destination_path, &staging->initial_destination_exists, &staging->initial_destination_identity, 0,
			"Could not inspect filesystem transaction destination"
		) != 0 ||
		create_stage(staging) != 0 ||
		regular_file_identity(staging->staging, &staging->staging_identity, "Could not inspect filesystem staging identity") !=
			0 ||
		current_stage_identity(staging) != 0) {
		if (staging->staging != INVALID_HANDLE_VALUE) {
			FILE_DISPOSITION_INFO disposition = {.DeleteFile = TRUE};
			SetFileInformationByHandle(staging->staging, FileDispositionInfo, &disposition, (DWORD)sizeof(disposition));
		}
		destroy_staging(staging);
		return NULL;
	}
	staging->state = DYNLEX_FILESYSTEM_STAGING_ACTIVE;
	return staging;
}

void dynlex_filesystem_staging_retain(void *opaque_staging) {
	DynlexWindowsStaging *staging = opaque_staging;
	if (staging == NULL)
		return;
	if (staging->references == SIZE_MAX)
		abort();
	staging->references++;
}

static int delete_stage(DynlexWindowsStaging *staging, int32_t failure_operation) {
	if (!staging->stage_name_present)
		return 0;
	if (current_stage_identity(staging) != 0)
		return -1;
	if (injected_failure(failure_operation, 0)) {
		dynlex_runtime_set_windows_error("Could not remove filesystem staging file", GetLastError());
		return -1;
	}
	DynlexFileDispositionInfoEx disposition = {
		.flags = DYNLEX_FILE_DISPOSITION_DELETE | DYNLEX_FILE_DISPOSITION_POSIX_SEMANTICS |
				 DYNLEX_FILE_DISPOSITION_IGNORE_READONLY_ATTRIBUTE
	};
	if (!SetFileInformationByHandle(
			staging->staging, DYNLEX_FILE_DISPOSITION_INFO_EX_CLASS, &disposition, (DWORD)sizeof(disposition)
		)) {
		dynlex_runtime_set_windows_error("Could not remove filesystem staging file", GetLastError());
		return -1;
	}
	CloseHandle(staging->staging);
	staging->staging = INVALID_HANDLE_VALUE;
	staging->stage_name_present = false;
	return 0;
}

void dynlex_filesystem_staging_release(void *opaque_staging) {
	DynlexWindowsStaging *staging = opaque_staging;
	if (staging == NULL)
		return;
	if (staging->references == 0)
		abort();
	if (--staging->references != 0)
		return;
	if (staging->state == DYNLEX_FILESYSTEM_STAGING_ACTIVE || staging->state == DYNLEX_FILESYSTEM_STAGING_SEALED ||
		staging->state == DYNLEX_FILESYSTEM_STAGING_POISONED || staging->state == DYNLEX_FILESYSTEM_STAGING_CLEANUP_FAILED)
		(void)delete_stage(staging, DYNLEX_FILESYSTEM_TEST_FAIL_CANCEL_UNLINK);
	destroy_staging(staging);
}

size_t dynlex_filesystem_staging_path_length(const void *opaque_staging) {
	const DynlexWindowsStaging *staging = opaque_staging;
	return staging == NULL ? 0 : staging->staging_path_length;
}

int dynlex_filesystem_staging_copy_path(const void *opaque_staging, char *buffer, size_t capacity) {
	dynlex_runtime_clear_error();
	const DynlexWindowsStaging *staging = opaque_staging;
	if (staging == NULL || buffer == NULL || capacity <= staging->staging_path_length) {
		dynlex_runtime_set_error("Filesystem staging path buffer is too small");
		return -1;
	}
	memcpy(buffer, staging->staging_path, staging->staging_path_length + 1);
	return 0;
}

int dynlex_filesystem_staging_state(const void *opaque_staging) {
	const DynlexWindowsStaging *staging = opaque_staging;
	return staging == NULL ? 0 : staging->state;
}

int dynlex_filesystem_staging_write(void *opaque_staging, const char *contents, size_t length) {
	dynlex_runtime_clear_error();
	DynlexWindowsStaging *staging = opaque_staging;
	if (staging == NULL || (contents == NULL && length > 0)) {
		dynlex_runtime_set_error("Invalid filesystem staging write arguments");
		return -1;
	}
	if (staging->state != DYNLEX_FILESYSTEM_STAGING_ACTIVE) {
		dynlex_runtime_set_error("Filesystem staging file is not active");
		return -1;
	}
	if (current_stage_identity(staging) != 0) {
		staging->state = DYNLEX_FILESYSTEM_STAGING_POISONED;
		return -1;
	}
	size_t written = 0;
	while (written < length) {
		if (injected_failure(DYNLEX_FILESYSTEM_TEST_FAIL_WRITE, written)) {
			dynlex_runtime_set_windows_error("Could not write filesystem staging file", GetLastError());
			staging->state = DYNLEX_FILESYSTEM_STAGING_POISONED;
			return -1;
		}
		size_t requested = injected_write_size(written, length - written);
		DWORD chunk = requested > MAXDWORD ? MAXDWORD : (DWORD)requested;
		DWORD transferred = 0;
		if (!WriteFile(staging->staging, contents + written, chunk, &transferred, NULL) || transferred == 0) {
			dynlex_runtime_set_windows_error("Could not write filesystem staging file", GetLastError());
			staging->state = DYNLEX_FILESYSTEM_STAGING_POISONED;
			return -1;
		}
		written += transferred;
	}
	return 0;
}

int dynlex_filesystem_staging_restore_metadata(
	void *opaque_staging, bool mode_supported, int64_t mode, bool windows_attributes_supported,
	int64_t restorable_windows_attributes, bool access_time_supported, int64_t access_seconds, int32_t access_nanoseconds,
	bool modification_time_supported, int64_t modification_seconds, int32_t modification_nanoseconds,
	bool creation_time_supported, int64_t creation_seconds, int32_t creation_nanoseconds
) {
	(void)mode_supported;
	(void)mode;
	dynlex_runtime_clear_error();
	DynlexWindowsStaging *staging = opaque_staging;
	if (staging == NULL) {
		dynlex_runtime_set_error("Invalid filesystem staging handle");
		return -1;
	}
	if (staging->state != DYNLEX_FILESYSTEM_STAGING_ACTIVE) {
		dynlex_runtime_set_error("Filesystem staging file is not active");
		return -1;
	}
	if (!windows_attributes_supported || !access_time_supported || !modification_time_supported || !creation_time_supported) {
		staging->state = DYNLEX_FILESYSTEM_STAGING_POISONED;
		dynlex_runtime_set_error("Required Windows filesystem metadata is unsupported");
		return -2;
	}
	FILETIME access;
	FILETIME modification;
	FILETIME creation;
	if (filetime_value(access_seconds, access_nanoseconds, &access) != 0 ||
		filetime_value(modification_seconds, modification_nanoseconds, &modification) != 0 ||
		filetime_value(creation_seconds, creation_nanoseconds, &creation) != 0 || current_stage_identity(staging) != 0) {
		staging->state = DYNLEX_FILESYSTEM_STAGING_POISONED;
		return -1;
	}
	if (injected_failure(DYNLEX_FILESYSTEM_TEST_FAIL_RESTORE_TIMES, 0) ||
		!SetFileTime(staging->staging, &creation, &access, &modification)) {
		dynlex_runtime_set_windows_error("Could not restore filesystem staging timestamps", GetLastError());
		staging->state = DYNLEX_FILESYSTEM_STAGING_POISONED;
		return -1;
	}
	DWORD restorable_mask = FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_ARCHIVE |
							FILE_ATTRIBUTE_NOT_CONTENT_INDEXED;
	DWORD attributes = (DWORD)restorable_windows_attributes & restorable_mask;
	if (attributes == 0)
		attributes = FILE_ATTRIBUTE_NORMAL;
	FILE_BASIC_INFO restored_attributes = {0};
	restored_attributes.FileAttributes = attributes;
	if (injected_failure(DYNLEX_FILESYSTEM_TEST_FAIL_RESTORE_MODE, 0) ||
		!SetFileInformationByHandle(
			staging->staging, FileBasicInfo, &restored_attributes, (DWORD)sizeof(restored_attributes)
		) ||
		current_stage_identity(staging) != 0) {
		dynlex_runtime_set_windows_error("Could not restore filesystem staging attributes", GetLastError());
		staging->state = DYNLEX_FILESYSTEM_STAGING_POISONED;
		return -1;
	}
	staging->state = DYNLEX_FILESYSTEM_STAGING_SEALED;
	return 0;
}

int dynlex_filesystem_staging_cancel(void *opaque_staging, int32_t *cleanup_succeeded) {
	dynlex_runtime_clear_error();
	DynlexWindowsStaging *staging = opaque_staging;
	if (staging == NULL || cleanup_succeeded == NULL) {
		dynlex_runtime_set_error("Invalid filesystem staging cancellation arguments");
		return -1;
	}
	*cleanup_succeeded = 0;
	if (staging->state == DYNLEX_FILESYSTEM_STAGING_COMMITTED || staging->state == DYNLEX_FILESYSTEM_STAGING_CANCELLED) {
		dynlex_runtime_set_error("Filesystem staging transaction is already terminal");
		return -1;
	}
	if (delete_stage(staging, DYNLEX_FILESYSTEM_TEST_FAIL_CANCEL_UNLINK) != 0) {
		staging->state = DYNLEX_FILESYSTEM_STAGING_CLEANUP_FAILED;
		return -1;
	}
	*cleanup_succeeded = 1;
	staging->state = staging->committed_destination ? DYNLEX_FILESYSTEM_STAGING_COMMITTED : DYNLEX_FILESYSTEM_STAGING_CANCELLED;
	return 0;
}

static int recheck_parent(DynlexWindowsStaging *staging) {
	FILE_ID_INFO held_identity;
	if (!GetFileInformationByHandleEx(staging->parent, FileIdInfo, &held_identity, (DWORD)sizeof(held_identity)) ||
		!same_identity(&held_identity, &staging->parent_identity)) {
		dynlex_runtime_set_error("Filesystem transaction parent identity changed");
		return -1;
	}
	HANDLE current = open_attributes(staging->parent_path, 0);
	if (current == INVALID_HANDLE_VALUE) {
		dynlex_runtime_set_windows_error("Could not recheck filesystem transaction parent", GetLastError());
		return -1;
	}
	FILE_ID_INFO current_identity;
	BOOL inspected = GetFileInformationByHandleEx(current, FileIdInfo, &current_identity, (DWORD)sizeof(current_identity));
	CloseHandle(current);
	if (!inspected || !same_identity(&current_identity, &held_identity)) {
		dynlex_runtime_set_error("Filesystem transaction parent path identity changed");
		return -1;
	}
	return 0;
}

static int precommit_recheck(DynlexWindowsStaging *staging, bool overwrite) {
	if (recheck_parent(staging) != 0 || current_stage_identity(staging) != 0)
		return -1;
	bool exists = false;
	FILE_ID_INFO identity;
	if (path_identity(
			staging->destination_path, &exists, &identity, 0, "Could not recheck filesystem transaction destination"
		) != 0)
		return -1;
	if (overwrite && (exists != staging->initial_destination_exists ||
					  (exists && !same_identity(&identity, &staging->initial_destination_identity)))) {
		dynlex_runtime_set_error("Filesystem transaction destination identity changed");
		return -1;
	}
	return 0;
}

static int cleanup_failed_commit(DynlexWindowsStaging *staging, int32_t *cleanup_succeeded, int result) {
	if (delete_stage(staging, DYNLEX_FILESYSTEM_TEST_FAIL_CANCEL_UNLINK) != 0) {
		staging->state = DYNLEX_FILESYSTEM_STAGING_CLEANUP_FAILED;
		return result;
	}
	*cleanup_succeeded = 1;
	staging->state = DYNLEX_FILESYSTEM_STAGING_CANCELLED;
	return result;
}

int dynlex_filesystem_staging_commit(
	void *opaque_staging, bool overwrite, bool require_durability, int32_t *outcome_known, int32_t *committed, int32_t *durable,
	int32_t *cleanup_succeeded
) {
	dynlex_runtime_clear_error();
	DynlexWindowsStaging *staging = opaque_staging;
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
	if (require_durability) {
		dynlex_runtime_set_error("Required filesystem transaction durability is unsupported on Windows");
		return cleanup_failed_commit(staging, cleanup_succeeded, -2);
	}
	if (precommit_recheck(staging, overwrite) != 0)
		return cleanup_failed_commit(staging, cleanup_succeeded, -1);
	if (injected_failure(DYNLEX_FILESYSTEM_TEST_FAIL_COMMIT, 0)) {
		dynlex_runtime_set_windows_error("Could not commit filesystem staging transaction", GetLastError());
		return cleanup_failed_commit(staging, cleanup_succeeded, -1);
	}
	size_t name_bytes = wcslen(staging->destination_path) * sizeof(wchar_t);
	if (name_bytes > MAXDWORD || name_bytes > SIZE_MAX - sizeof(DynlexFileRenameInfo)) {
		dynlex_runtime_set_error("Filesystem transaction destination is too large");
		return cleanup_failed_commit(staging, cleanup_succeeded, -1);
	}
	size_t information_size = offsetof(DynlexFileRenameInfo, file_name) + name_bytes;
	DynlexFileRenameInfo *information = calloc(1, information_size);
	if (information == NULL) {
		dynlex_runtime_set_errno_error("Could not allocate filesystem rename information", ENOMEM);
		return cleanup_failed_commit(staging, cleanup_succeeded, -1);
	}
	information->flags = overwrite ? DYNLEX_FILE_RENAME_REPLACE_IF_EXISTS | DYNLEX_FILE_RENAME_POSIX_SEMANTICS : 0;
	information->root_directory = NULL;
	information->file_name_length = (DWORD)name_bytes;
	memcpy(information->file_name, staging->destination_path, name_bytes);
	FILE_INFO_BY_HANDLE_CLASS rename_class = overwrite ? DYNLEX_FILE_RENAME_INFO_EX_CLASS : DYNLEX_FILE_RENAME_INFO_CLASS;
	BOOL renamed = SetFileInformationByHandle(staging->staging, rename_class, information, (DWORD)information_size);
	DWORD error_number = GetLastError();
	free(information);
	if (!renamed) {
		dynlex_runtime_set_windows_error("Could not commit filesystem staging transaction", error_number);
		return cleanup_failed_commit(staging, cleanup_succeeded, -1);
	}
	staging->stage_name_present = false;
	staging->committed_destination = true;
	*committed = 1;
	bool destination_exists = false;
	FILE_ID_INFO destination_identity;
	if (path_identity(
			staging->destination_path, &destination_exists, &destination_identity, 0,
			"Could not verify committed filesystem destination"
		) != 0 ||
		!destination_exists || !same_identity(&destination_identity, &staging->staging_identity)) {
		dynlex_runtime_set_error("Committed filesystem destination identity does not match the staging file");
		staging->state = DYNLEX_FILESYSTEM_STAGING_CLEANUP_FAILED;
		return -1;
	}
	*cleanup_succeeded = 1;
	staging->state = DYNLEX_FILESYSTEM_STAGING_COMMITTED;
	return 0;
}
