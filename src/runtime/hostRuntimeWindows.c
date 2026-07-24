#include "hostRuntimeInternal.h"

#include "hostRuntimeWindowsPath.h"
#include "runtimeError.h"

#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

int dynlex_platform_executable_path(char **path, size_t *length, int32_t *supported) {
	if (path == NULL || length == NULL || supported == NULL) {
		dynlex_runtime_set_windows_error("Invalid executable path result arguments", ERROR_INVALID_PARAMETER);
		return -1;
	}
	*path = NULL;
	*length = 0;
	*supported = 1;

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
	DynlexWindowsPathNamespace path_namespace = dynlex_windows_path_namespace(wide_path, wide_length);
	if (path_namespace != DYNLEX_WINDOWS_PATH_REGULAR) {
		free(wide_path);
		dynlex_runtime_set_error(
			path_namespace == DYNLEX_WINDOWS_PATH_EXTENDED ? "The executable path uses unsupported extended Windows path syntax"
														   : "The executable path uses an unsupported Windows device namespace"
		);
		return -1;
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

int dynlex_platform_prepare_standard_input(void) {
	if (_setmode(_fileno(stdin), _O_BINARY) == -1) {
		dynlex_runtime_set_errno_error("Could not set standard input to binary mode", errno);
		return -1;
	}
	return 0;
}
