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

static wchar_t *host_utf8_to_wide(const char *value, size_t length, const char *description) {
	if (length > INT32_MAX || memchr(value, '\0', length) != NULL) {
		dynlex_runtime_set_error(description);
		return NULL;
	}
	int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, (int)length, NULL, 0);
	if (count <= 0 && length != 0) {
		dynlex_runtime_set_windows_error(description, GetLastError());
		return NULL;
	}
	wchar_t *wide = malloc(((size_t)count + 1) * sizeof(wchar_t));
	if (wide == NULL) {
		dynlex_runtime_set_windows_error("Could not allocate host input", ERROR_OUTOFMEMORY);
		return NULL;
	}
	if (count != 0)
		MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, (int)length, wide, count);
	wide[count] = L'\0';
	return wide;
}

static int host_wide_to_utf8(const wchar_t *wide, size_t wide_length, char **value, size_t *length) {
	int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, (int)wide_length, NULL, 0, NULL, NULL);
	if (count <= 0 && wide_length != 0) {
		dynlex_runtime_set_windows_error("Could not encode host text as UTF-8", GetLastError());
		return -1;
	}
	char *text = malloc(count == 0 ? 1 : (size_t)count);
	if (text == NULL) {
		dynlex_runtime_set_windows_error("Could not allocate host text", ERROR_OUTOFMEMORY);
		return -1;
	}
	if (count != 0)
		WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, (int)wide_length, text, count, NULL, NULL);
	*value = text;
	*length = (size_t)count;
	return 0;
}

int dynlex_platform_environment_value(
	const char *name, size_t name_length, char **value, size_t *length, int32_t *found
) {
	*value = NULL;
	*length = 0;
	*found = 0;
	if (name_length == 0 || memchr(name, '=', name_length) != NULL) {
		dynlex_runtime_set_error("Invalid environment variable name");
		return -1;
	}
	wchar_t *wide_name = host_utf8_to_wide(name, name_length, "Invalid environment variable name");
	if (wide_name == NULL)
		return -1;
	SetLastError(ERROR_SUCCESS);
	DWORD required = GetEnvironmentVariableW(wide_name, NULL, 0);
	DWORD error_number = GetLastError();
	if (required == 0 && error_number == ERROR_ENVVAR_NOT_FOUND) {
		free(wide_name);
		return 0;
	}
	if (required == 0 && error_number != ERROR_SUCCESS) {
		free(wide_name);
		dynlex_runtime_set_windows_error("Could not read environment variable", error_number);
		return -1;
	}
	if (required == 0) {
		free(wide_name);
		*value = malloc(1);
		if (*value == NULL) {
			dynlex_runtime_set_windows_error("Could not allocate environment variable value", ERROR_OUTOFMEMORY);
			return -1;
		}
		*found = 1;
		return 0;
	}
	wchar_t *wide_value = malloc((size_t)required * sizeof(wchar_t));
	if (wide_value == NULL) {
		free(wide_name);
		dynlex_runtime_set_windows_error("Could not allocate environment variable value", ERROR_OUTOFMEMORY);
		return -1;
	}
	DWORD copied = GetEnvironmentVariableW(wide_name, wide_value, required);
	free(wide_name);
	if (copied == 0 || copied >= required) {
		error_number = GetLastError();
		free(wide_value);
		dynlex_runtime_set_windows_error("Could not read environment variable", error_number);
		return -1;
	}
	int result = host_wide_to_utf8(wide_value, copied, value, length);
	free(wide_value);
	if (result == 0)
		*found = 1;
	return result;
}

int dynlex_platform_find_executable(
	const char *name, size_t name_length, char **path, size_t *length, int32_t *found
) {
	*path = NULL;
	*length = 0;
	*found = 0;
	wchar_t *wide_name = host_utf8_to_wide(name, name_length, "Invalid executable name");
	if (wide_name == NULL)
		return -1;
	SetLastError(ERROR_SUCCESS);
	DWORD path_capacity = GetEnvironmentVariableW(L"PATH", NULL, 0);
	DWORD error_number = GetLastError();
	if (path_capacity == 0) {
		free(wide_name);
		if (error_number == ERROR_SUCCESS || error_number == ERROR_ENVVAR_NOT_FOUND)
			return 0;
		dynlex_runtime_set_windows_error("Could not read PATH", error_number);
		return -1;
	}
	wchar_t *search_path = malloc((size_t)path_capacity * sizeof(wchar_t));
	if (search_path == NULL) {
		free(wide_name);
		dynlex_runtime_set_windows_error("Could not allocate PATH", ERROR_OUTOFMEMORY);
		return -1;
	}
	DWORD path_length = GetEnvironmentVariableW(L"PATH", search_path, path_capacity);
	if (path_length == 0 || path_length >= path_capacity) {
		error_number = GetLastError();
		free(search_path);
		free(wide_name);
		dynlex_runtime_set_windows_error("Could not read PATH", error_number);
		return -1;
	}
	DWORD required = SearchPathW(search_path, wide_name, L".exe", 0, NULL, NULL);
	if (required == 0) {
		error_number = GetLastError();
		free(search_path);
		free(wide_name);
		if (error_number == ERROR_FILE_NOT_FOUND || error_number == ERROR_PATH_NOT_FOUND)
			return 0;
		dynlex_runtime_set_windows_error("Could not search for executable", error_number);
		return -1;
	}
	wchar_t *wide_path = malloc(((size_t)required + 1) * sizeof(wchar_t));
	if (wide_path == NULL) {
		free(search_path);
		free(wide_name);
		dynlex_runtime_set_windows_error("Could not allocate executable path", ERROR_OUTOFMEMORY);
		return -1;
	}
	DWORD copied = SearchPathW(search_path, wide_name, L".exe", required + 1, wide_path, NULL);
	free(search_path);
	free(wide_name);
	if (copied == 0 || copied > required) {
		DWORD error_number = GetLastError();
		free(wide_path);
		dynlex_runtime_set_windows_error("Could not search for executable", error_number);
		return -1;
	}
	int result = absolute_windows_path_as_utf8(wide_path, copied, path, length);
	free(wide_path);
	if (result == 0)
		*found = 1;
	return result;
}

int dynlex_platform_is_administrator(int32_t *administrator) {
	SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY;
	PSID group = NULL;
	if (!AllocateAndInitializeSid(&authority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &group)) {
		dynlex_runtime_set_windows_error("Could not create administrator group identifier", GetLastError());
		return -1;
	}
	BOOL member = FALSE;
	BOOL succeeded = CheckTokenMembership(NULL, group, &member);
	DWORD error_number = GetLastError();
	FreeSid(group);
	if (!succeeded) {
		dynlex_runtime_set_windows_error("Could not inspect administrator privileges", error_number);
		return -1;
	}
	*administrator = member ? 1 : 0;
	return 0;
}

const char *dynlex_platform_name(void) { return "Windows"; }
