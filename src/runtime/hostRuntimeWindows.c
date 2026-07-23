#include "hostRuntimeInternal.h"

#include "runtimeError.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

static bool ascii_prefix_equal_case_insensitive(const char *text, const char *prefix, size_t length) {
	for (size_t index = 0; index < length; ++index) {
		char left = text[index];
		char right = prefix[index];
		if (left >= 'a' && left <= 'z')
			left = (char)(left - ('a' - 'A'));
		if (right >= 'a' && right <= 'z')
			right = (char)(right - ('a' - 'A'));
		if (left != right)
			return false;
	}
	return true;
}

int dynlex_platform_executable_path(char **path, size_t *length) {
	if (path == NULL || length == NULL) {
		dynlex_runtime_set_windows_error("Invalid executable path result arguments", ERROR_INVALID_PARAMETER);
		return -1;
	}
	*path = NULL;
	*length = 0;

	DWORD capacity = 256;
	wchar_t *wide_path = NULL;
	DWORD wide_length = 0;
	for (;;) {
		wide_path = malloc((size_t)capacity * sizeof(wchar_t));
		if (wide_path == NULL) {
			dynlex_runtime_set_windows_error("Could not allocate executable path", ERROR_OUTOFMEMORY);
			return -1;
		}
		SetLastError(ERROR_SUCCESS);
		wide_length = GetModuleFileNameW(NULL, wide_path, capacity);
		DWORD error_number = GetLastError();
		if (wide_length == 0) {
			free(wide_path);
			dynlex_runtime_set_windows_error("Could not retrieve executable path", error_number);
			return -1;
		}
		if (wide_length < capacity)
			break;
		free(wide_path);
		wide_path = NULL;
		if (capacity > (DWORD)INT32_MAX / 2) {
			dynlex_runtime_set_error("Executable path exceeds the maximum string length");
			return -1;
		}
		capacity *= 2;
	}

	int utf8_length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide_path, (int)wide_length, NULL, 0, NULL, NULL);
	if (utf8_length <= 0) {
		DWORD error_number = GetLastError();
		free(wide_path);
		dynlex_runtime_set_windows_error("Could not encode executable path as UTF-8", error_number);
		return -1;
	}
	char *utf8_path = malloc((size_t)utf8_length);
	if (utf8_path == NULL) {
		free(wide_path);
		dynlex_runtime_set_windows_error("Could not allocate UTF-8 executable path", ERROR_OUTOFMEMORY);
		return -1;
	}
	int converted =
		WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide_path, (int)wide_length, utf8_path, utf8_length, NULL, NULL);
	free(wide_path);
	if (converted != utf8_length) {
		DWORD error_number = GetLastError();
		free(utf8_path);
		dynlex_runtime_set_windows_error("Could not encode executable path as UTF-8", error_number);
		return -1;
	}
	for (int index = 0; index < utf8_length; ++index) {
		if (utf8_path[index] == '\\')
			utf8_path[index] = '/';
	}
	if (utf8_length >= 8 && ascii_prefix_equal_case_insensitive(utf8_path, "//?/UNC/", 8)) {
		memmove(utf8_path + 2, utf8_path + 8, (size_t)utf8_length - 8);
		utf8_length -= 6;
	} else if (utf8_length >= 4 && memcmp(utf8_path, "//?/", 4) == 0) {
		memmove(utf8_path, utf8_path + 4, (size_t)utf8_length - 4);
		utf8_length -= 4;
	} else if (utf8_length >= 4 && memcmp(utf8_path, "//./", 4) == 0) {
		free(utf8_path);
		dynlex_runtime_set_error("The executable path uses an unsupported Windows device namespace");
		return -1;
	}
	bool absolute_drive_path = utf8_length >= 3 &&
							   ((utf8_path[0] >= 'A' && utf8_path[0] <= 'Z') || (utf8_path[0] >= 'a' && utf8_path[0] <= 'z')) &&
							   utf8_path[1] == ':' && utf8_path[2] == '/';
	bool absolute_unc_path = utf8_length >= 5 && utf8_path[0] == '/' && utf8_path[1] == '/';
	if (!absolute_drive_path && !absolute_unc_path) {
		free(utf8_path);
		dynlex_runtime_set_error("The operating system returned a non-absolute executable path");
		return -1;
	}
	*path = utf8_path;
	*length = (size_t)utf8_length;
	return 0;
}
