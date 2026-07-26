#include "hostRuntimeInternal.h"

#include "hostRuntimeWindowsPath.h"
#include "runtimeError.h"

#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <shlobj.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <windows.h>

static int absolute_windows_path_as_utf8(const wchar_t *wide_path, size_t wide_length, char **path, size_t *length) {
	DynlexWindowsPathNamespace path_namespace = dynlex_windows_path_namespace(wide_path, wide_length);
	if (path_namespace != DYNLEX_WINDOWS_PATH_REGULAR) {
		dynlex_runtime_set_error(
			path_namespace == DYNLEX_WINDOWS_PATH_EXTENDED ? "The host path uses unsupported extended Windows path syntax"
														   : "The host path uses an unsupported Windows device namespace"
		);
		return -1;
	}

	int utf8_length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide_path, (int)wide_length, NULL, 0, NULL, NULL);
	if (utf8_length <= 0) {
		DWORD error_number = GetLastError();
		dynlex_runtime_set_windows_error("Could not encode host path as UTF-8", error_number);
		return -1;
	}
	char *utf8_path = malloc((size_t)utf8_length);
	if (utf8_path == NULL) {
		dynlex_runtime_set_windows_error("Could not allocate UTF-8 host path", ERROR_OUTOFMEMORY);
		return -1;
	}
	int converted =
		WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide_path, (int)wide_length, utf8_path, utf8_length, NULL, NULL);
	if (converted != utf8_length) {
		DWORD error_number = GetLastError();
		free(utf8_path);
		dynlex_runtime_set_windows_error("Could not encode host path as UTF-8", error_number);
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
		dynlex_runtime_set_error("The operating system returned a non-absolute host path");
		return -1;
	}
	*path = utf8_path;
	*length = (size_t)utf8_length;
	return 0;
}

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
	int result = absolute_windows_path_as_utf8(wide_path, wide_length, path, length);
	free(wide_path);
	return result;
}

int dynlex_platform_user_cache_directory(char **path, size_t *length, int32_t *supported) {
	if (path == NULL || length == NULL || supported == NULL) {
		dynlex_runtime_set_windows_error("Invalid user cache directory result arguments", ERROR_INVALID_PARAMETER);
		return -1;
	}
	*path = NULL;
	*length = 0;
	*supported = 1;

	PWSTR wide_path = NULL;
	HRESULT status = SHGetKnownFolderPath(&FOLDERID_LocalAppData, KF_FLAG_DEFAULT, NULL, &wide_path);
	if (FAILED(status)) {
		dynlex_runtime_set_windows_error("Could not retrieve the user cache directory", (DWORD)status);
		return -1;
	}
	int result = absolute_windows_path_as_utf8(wide_path, wcslen(wide_path), path, length);
	CoTaskMemFree(wide_path);
	return result;
}

int dynlex_platform_prepare_standard_input(void) {
	if (_setmode(_fileno(stdin), _O_BINARY) == -1) {
		dynlex_runtime_set_errno_error("Could not set standard input to binary mode", errno);
		return -1;
	}
	return 0;
}
