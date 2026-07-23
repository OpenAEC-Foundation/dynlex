#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <locale.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#endif

#if defined(_MSC_VER)
#define DYNLEX_THREAD_LOCAL __declspec(thread)
#else
#define DYNLEX_THREAD_LOCAL _Thread_local
#endif

static DYNLEX_THREAD_LOCAL char filesystem_error_message[512];

#if !defined(_WIN32)
static void copy_error_message(const char *message) {
	size_t length = strlen(message);
	if (length >= sizeof(filesystem_error_message))
		length = sizeof(filesystem_error_message) - 1;
	memcpy(filesystem_error_message, message, length);
	filesystem_error_message[length] = '\0';
}
#endif

static void set_errno_error(int error_number) {
	errno = error_number;
#if defined(_WIN32)
	if (strerror_s(filesystem_error_message, sizeof(filesystem_error_message), error_number) != 0)
		snprintf(filesystem_error_message, sizeof(filesystem_error_message), "C runtime error %d", error_number);
#else
	locale_t english_locale = newlocale(LC_MESSAGES_MASK, "C", (locale_t)0);
	if (english_locale == (locale_t)0)
		snprintf(filesystem_error_message, sizeof(filesystem_error_message), "Filesystem error %d", error_number);
	else {
		copy_error_message(strerror_l(error_number, english_locale));
		freelocale(english_locale);
	}
#endif
	errno = error_number;
}

#ifdef _WIN32
static void set_windows_error(DWORD error_number) {
	DWORD length = FormatMessageA(
		FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, error_number,
		MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), filesystem_error_message,
		(DWORD)sizeof(filesystem_error_message), NULL
	);
	if (length == 0) {
		snprintf(filesystem_error_message, sizeof(filesystem_error_message), "Windows filesystem error %lu", error_number);
		return;
	}
	while (length > 0 && (filesystem_error_message[length - 1] == '\r' || filesystem_error_message[length - 1] == '\n'))
		filesystem_error_message[--length] = '\0';
}
#endif

void dynlex_filesystem_clear_error(void) {
	filesystem_error_message[0] = '\0';
	errno = 0;
}

size_t dynlex_filesystem_error_message(char *buffer, size_t capacity) {
	if (filesystem_error_message[0] == '\0')
		set_errno_error(errno == 0 ? EIO : errno);

	size_t length = strlen(filesystem_error_message);
	if (buffer != NULL && capacity > 0) {
		size_t copied = length < capacity - 1 ? length : capacity - 1;
		memcpy(buffer, filesystem_error_message, copied);
		buffer[copied] = '\0';
	}
	return length;
}

int dynlex_filesystem_status(const char *path, int32_t *regular_file, int64_t *modification_time) {
	dynlex_filesystem_clear_error();
	if (path == NULL || regular_file == NULL || modification_time == NULL) {
		set_errno_error(EINVAL);
		return -1;
	}

#ifdef _WIN32
	WIN32_FILE_ATTRIBUTE_DATA attributes;
	if (!GetFileAttributesExA(path, GetFileExInfoStandard, &attributes)) {
		set_windows_error(GetLastError());
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
		set_errno_error(errno);
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
		set_errno_error(EINVAL);
		return -1;
	}

#ifdef _WIN32
	if (!CreateDirectoryA(path, NULL)) {
		set_windows_error(GetLastError());
		return -1;
	}
#else
	if (mkdir(path, 0777) != 0) {
		set_errno_error(errno);
		return -1;
	}
#endif
	return 0;
}

int dynlex_filesystem_remove(const char *path) {
	dynlex_filesystem_clear_error();
	if (path == NULL) {
		set_errno_error(EINVAL);
		return -1;
	}

#ifdef _WIN32
	DWORD attributes = GetFileAttributesA(path);
	if (attributes == INVALID_FILE_ATTRIBUTES) {
		set_windows_error(GetLastError());
		return -1;
	}
	BOOL removed = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ? RemoveDirectoryA(path) : DeleteFileA(path);
	if (!removed) {
		set_windows_error(GetLastError());
		return -1;
	}
#else
	if (remove(path) != 0) {
		set_errno_error(errno);
		return -1;
	}
#endif
	return 0;
}
