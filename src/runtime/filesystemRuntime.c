#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdint.h>
#include <stdio.h>

#include "runtimeError.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#endif

void dynlex_filesystem_clear_error(void) { dynlex_runtime_clear_error(); }

size_t dynlex_filesystem_error_message(char *buffer, size_t capacity) {
	if (dynlex_runtime_error_message(NULL, 0) == 0)
		dynlex_runtime_set_errno_error("Filesystem operation failed", errno == 0 ? EIO : errno);
	return dynlex_runtime_error_message(buffer, capacity);
}

int dynlex_filesystem_status(const char *path, int32_t *regular_file, int64_t *modification_time) {
	dynlex_filesystem_clear_error();
	if (path == NULL || regular_file == NULL || modification_time == NULL) {
		dynlex_runtime_set_errno_error("Invalid filesystem status arguments", EINVAL);
		return -1;
	}

#ifdef _WIN32
	WIN32_FILE_ATTRIBUTE_DATA attributes;
	if (!GetFileAttributesExA(path, GetFileExInfoStandard, &attributes)) {
		dynlex_runtime_set_windows_error("Could not read filesystem status", GetLastError());
		return -1;
	}
	ULARGE_INTEGER file_time;
	file_time.LowPart = attributes.ftLastWriteTime.dwLowDateTime;
	file_time.HighPart = attributes.ftLastWriteTime.dwHighDateTime;
	*regular_file = (attributes.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ? 1 : 0;
	*modification_time = (int64_t)(file_time.QuadPart / 10000ULL) - INT64_C(11644473600000);
#else
	struct stat attributes;
	if (stat(path, &attributes) != 0) {
		dynlex_runtime_set_errno_error("Could not read filesystem status", errno);
		return -1;
	}
	*regular_file = S_ISREG(attributes.st_mode) ? 1 : 0;
#if defined(__APPLE__)
	*modification_time = (int64_t)attributes.st_mtimespec.tv_sec * INT64_C(1000) + attributes.st_mtimespec.tv_nsec / 1000000;
#else
	*modification_time = (int64_t)attributes.st_mtim.tv_sec * INT64_C(1000) + attributes.st_mtim.tv_nsec / 1000000;
#endif
#endif
	return 0;
}

int dynlex_filesystem_create_directory(const char *path) {
	dynlex_filesystem_clear_error();
	if (path == NULL) {
		dynlex_runtime_set_errno_error("Invalid directory path", EINVAL);
		return -1;
	}

#ifdef _WIN32
	if (!CreateDirectoryA(path, NULL)) {
		dynlex_runtime_set_windows_error("Could not create directory", GetLastError());
		return -1;
	}
#else
	if (mkdir(path, 0777) != 0) {
		dynlex_runtime_set_errno_error("Could not create directory", errno);
		return -1;
	}
#endif
	return 0;
}

int dynlex_filesystem_remove(const char *path) {
	dynlex_filesystem_clear_error();
	if (path == NULL) {
		dynlex_runtime_set_errno_error("Invalid removal path", EINVAL);
		return -1;
	}

#ifdef _WIN32
	DWORD attributes = GetFileAttributesA(path);
	if (attributes == INVALID_FILE_ATTRIBUTES) {
		dynlex_runtime_set_windows_error("Could not inspect removal path", GetLastError());
		return -1;
	}
	BOOL removed = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ? RemoveDirectoryA(path) : DeleteFileA(path);
	if (!removed) {
		dynlex_runtime_set_windows_error("Could not remove filesystem entry", GetLastError());
		return -1;
	}
#else
	if (remove(path) != 0) {
		dynlex_runtime_set_errno_error("Could not remove filesystem entry", errno);
		return -1;
	}
#endif
	return 0;
}
