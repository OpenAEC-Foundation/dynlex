#include "filesystemRuntimeInternal.h"

#include "runtimeError.h"

#include <errno.h>
#include <io.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>
#include <winioctl.h>

typedef struct {
	size_t references;
	HANDLE search;
	WIN32_FIND_DATAW current;
	bool has_current;
	char *current_name;
	size_t current_name_length;
} DynlexWindowsDirectory;

wchar_t *dynlex_platform_filesystem_wide_path(const char *path, size_t length) {
	if (!dynlex_filesystem_utf8_is_valid(path, length)) {
		dynlex_runtime_set_error("Filesystem paths must be nonempty UTF-8 text without zero bytes");
		return NULL;
	}
	if (length > INT_MAX) {
		dynlex_runtime_set_error("Filesystem path exceeds the Windows conversion limit");
		return NULL;
	}
	int wide_length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, (int)length, NULL, 0);
	if (wide_length <= 0) {
		dynlex_runtime_set_windows_error("Could not decode UTF-8 filesystem path", GetLastError());
		return NULL;
	}
	if ((size_t)wide_length >= SIZE_MAX / sizeof(wchar_t)) {
		dynlex_runtime_set_error("Filesystem path is too large");
		return NULL;
	}
	wchar_t *result = malloc(((size_t)wide_length + 1) * sizeof(wchar_t));
	if (result == NULL) {
		dynlex_runtime_set_errno_error("Could not allocate Windows filesystem path", ENOMEM);
		return NULL;
	}
	if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, (int)length, result, wide_length) != wide_length) {
		DWORD error_number = GetLastError();
		free(result);
		dynlex_runtime_set_windows_error("Could not decode UTF-8 filesystem path", error_number);
		return NULL;
	}
	result[wide_length] = L'\0';
	return result;
}

char *dynlex_platform_filesystem_utf8_text(const wchar_t *text, size_t *length) {
	size_t wide_length = wcslen(text);
	if (wide_length > INT_MAX) {
		dynlex_runtime_set_error("Windows filesystem name exceeds the conversion limit");
		return NULL;
	}
	int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, (int)wide_length, NULL, 0, NULL, NULL);
	if (required <= 0) {
		dynlex_runtime_set_windows_error("Could not encode Windows filesystem name", GetLastError());
		return NULL;
	}
	char *result = malloc((size_t)required + 1);
	if (result == NULL) {
		dynlex_runtime_set_errno_error("Could not allocate UTF-8 filesystem name", ENOMEM);
		return NULL;
	}
	if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, (int)wide_length, result, required, NULL, NULL) != required) {
		DWORD error_number = GetLastError();
		free(result);
		dynlex_runtime_set_windows_error("Could not encode Windows filesystem name", error_number);
		return NULL;
	}
	result[required] = '\0';
	*length = (size_t)required;
	return result;
}

static bool missing_error(DWORD error_number) {
	return error_number == ERROR_FILE_NOT_FOUND || error_number == ERROR_PATH_NOT_FOUND || error_number == ERROR_INVALID_NAME;
}

static bool link_tag(DWORD tag) { return tag == IO_REPARSE_TAG_SYMLINK || tag == IO_REPARSE_TAG_MOUNT_POINT; }

static size_t root_length(const wchar_t *path) {
	if (((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z')) && path[1] == L':')
		return (path[2] == L'/' || path[2] == L'\\') ? 3 : 2;
	if ((path[0] == L'/' || path[0] == L'\\') && (path[1] == L'/' || path[1] == L'\\')) {
		size_t separators = 0;
		for (size_t index = 2; path[index] != L'\0'; ++index) {
			if (path[index] == L'/' || path[index] == L'\\') {
				if (++separators == 2)
					return index + 1;
			}
		}
		return wcslen(path);
	}
	return (path[0] == L'/' || path[0] == L'\\') ? 1 : 0;
}

static void trim_trailing_separators(wchar_t *path) {
	size_t length = wcslen(path);
	size_t root = root_length(path);
	while (length > root && (path[length - 1] == L'/' || path[length - 1] == L'\\'))
		path[--length] = L'\0';
}

static int32_t find_entry_kind(const WIN32_FIND_DATAW *entry) {
	if ((entry->dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
		return link_tag(entry->dwReserved0) ? DYNLEX_FILESYSTEM_ENTRY_SYMBOLIC_LINK : DYNLEX_FILESYSTEM_ENTRY_OTHER;
	if ((entry->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
		return DYNLEX_FILESYSTEM_ENTRY_DIRECTORY;
	if ((entry->dwFileAttributes & FILE_ATTRIBUTE_DEVICE) == 0)
		return DYNLEX_FILESYSTEM_ENTRY_REGULAR_FILE;
	return DYNLEX_FILESYSTEM_ENTRY_OTHER;
}

static int path_entry_kind(const wchar_t *path, DWORD attributes, int32_t *kind) {
	if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0) {
		*kind = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ? DYNLEX_FILESYSTEM_ENTRY_DIRECTORY
				: (attributes & FILE_ATTRIBUTE_DEVICE) != 0	 ? DYNLEX_FILESYSTEM_ENTRY_OTHER
															 : DYNLEX_FILESYSTEM_ENTRY_REGULAR_FILE;
		return 0;
	}
	HANDLE handle = CreateFileW(
		path, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
		FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, NULL
	);
	if (handle == INVALID_HANDLE_VALUE) {
		dynlex_runtime_set_windows_error("Could not inspect filesystem reparse point", GetLastError());
		return -1;
	}
	FILE_ATTRIBUTE_TAG_INFO tag;
	BOOL inspected = GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &tag, (DWORD)sizeof(tag));
	DWORD error_number = GetLastError();
	CloseHandle(handle);
	if (!inspected) {
		dynlex_runtime_set_windows_error("Could not inspect filesystem reparse point", error_number);
		return -1;
	}
	*kind = link_tag(tag.ReparseTag) ? DYNLEX_FILESYSTEM_ENTRY_SYMBOLIC_LINK : DYNLEX_FILESYSTEM_ENTRY_OTHER;
	return 0;
}

FILE *dynlex_platform_filesystem_open_file(const char *path, size_t path_length, int32_t mode) {
	wchar_t *prepared = dynlex_platform_filesystem_wide_path(path, path_length);
	if (prepared == NULL)
		return NULL;
	trim_trailing_separators(prepared);
	const wchar_t *mode_text = mode == DYNLEX_FILESYSTEM_OPEN_READ	   ? L"rb"
							   : mode == DYNLEX_FILESYSTEM_OPEN_WRITE  ? L"wb"
							   : mode == DYNLEX_FILESYSTEM_OPEN_APPEND ? L"ab"
																	   : NULL;
	if (mode_text == NULL) {
		free(prepared);
		dynlex_runtime_set_error("Invalid filesystem open mode");
		return NULL;
	}
	FILE *file = NULL;
	errno_t open_result = _wfopen_s(&file, prepared, mode_text);
	free(prepared);
	if (open_result != 0 || file == NULL)
		dynlex_runtime_set_errno_error("Could not open file", open_result == 0 ? EIO : open_result);
	return file;
}

int dynlex_filesystem_status(const char *path, size_t path_length, int32_t *kind, int64_t *modification_time) {
	dynlex_runtime_clear_error();
	if (kind == NULL || modification_time == NULL) {
		dynlex_runtime_set_error("Invalid filesystem status outputs");
		return -1;
	}
	wchar_t *prepared = dynlex_platform_filesystem_wide_path(path, path_length);
	if (prepared == NULL)
		return -1;
	trim_trailing_separators(prepared);
	WIN32_FILE_ATTRIBUTE_DATA attributes;
	if (!GetFileAttributesExW(prepared, GetFileExInfoStandard, &attributes)) {
		DWORD error_number = GetLastError();
		free(prepared);
		if (missing_error(error_number))
			return 0;
		dynlex_runtime_set_windows_error("Could not read filesystem status", error_number);
		return -1;
	}
	if (path_entry_kind(prepared, attributes.dwFileAttributes, kind) != 0) {
		free(prepared);
		return -1;
	}
	free(prepared);
	ULARGE_INTEGER timestamp;
	timestamp.LowPart = attributes.ftLastWriteTime.dwLowDateTime;
	timestamp.HighPart = attributes.ftLastWriteTime.dwHighDateTime;
	*modification_time = (int64_t)(timestamp.QuadPart / 10000ULL) - INT64_C(11644473600000);
	return 1;
}

static wchar_t *directory_pattern(const char *path, size_t path_length) {
	wchar_t *prepared = dynlex_platform_filesystem_wide_path(path, path_length);
	if (prepared == NULL)
		return NULL;
	trim_trailing_separators(prepared);
	size_t length = wcslen(prepared);
	if (length > SIZE_MAX - 3 || (length + 3) > SIZE_MAX / sizeof(wchar_t)) {
		free(prepared);
		dynlex_runtime_set_error("Directory path is too large");
		return NULL;
	}
	wchar_t *pattern = realloc(prepared, (length + 3) * sizeof(wchar_t));
	if (pattern == NULL) {
		free(prepared);
		dynlex_runtime_set_errno_error("Could not allocate directory search path", ENOMEM);
		return NULL;
	}
	if (length > 0 && pattern[length - 1] != L'/' && pattern[length - 1] != L'\\')
		pattern[length++] = L'\\';
	pattern[length++] = L'*';
	pattern[length] = L'\0';
	return pattern;
}

void *dynlex_filesystem_directory_open(const char *path, size_t path_length) {
	dynlex_runtime_clear_error();
	int32_t kind = 0;
	int64_t ignored_time = 0;
	int status = dynlex_filesystem_status(path, path_length, &kind, &ignored_time);
	if (status != 1 || kind != DYNLEX_FILESYSTEM_ENTRY_DIRECTORY) {
		if (status == 0)
			dynlex_runtime_set_windows_error("Could not open directory", ERROR_PATH_NOT_FOUND);
		else if (status == 1)
			dynlex_runtime_set_error("Filesystem path is not a directory");
		return NULL;
	}
	wchar_t *pattern = directory_pattern(path, path_length);
	if (pattern == NULL)
		return NULL;
	DynlexWindowsDirectory *directory = calloc(1, sizeof(*directory));
	if (directory == NULL) {
		free(pattern);
		dynlex_runtime_set_errno_error("Could not allocate directory enumeration", ENOMEM);
		return NULL;
	}
	directory->search = FindFirstFileW(pattern, &directory->current);
	DWORD error_number = GetLastError();
	free(pattern);
	if (directory->search == INVALID_HANDLE_VALUE) {
		free(directory);
		dynlex_runtime_set_windows_error("Could not enumerate directory", error_number);
		return NULL;
	}
	directory->has_current = true;
	return directory;
}

int dynlex_filesystem_directory_next(DynlexWindowsDirectory *directory, int32_t *kind, size_t *name_length) {
	dynlex_runtime_clear_error();
	if (directory == NULL || kind == NULL || name_length == NULL) {
		dynlex_runtime_set_error("Invalid directory enumeration arguments");
		return -1;
	}
	for (;;) {
		if (!directory->has_current) {
			if (!FindNextFileW(directory->search, &directory->current)) {
				DWORD error_number = GetLastError();
				if (error_number == ERROR_NO_MORE_FILES)
					return 0;
				dynlex_runtime_set_windows_error("Could not enumerate directory", error_number);
				return -1;
			}
		}
		directory->has_current = false;
		if (wcscmp(directory->current.cFileName, L".") == 0 || wcscmp(directory->current.cFileName, L"..") == 0)
			continue;
		size_t length = 0;
		char *name = dynlex_platform_filesystem_utf8_text(directory->current.cFileName, &length);
		if (name == NULL)
			return -1;
		free(directory->current_name);
		directory->current_name = name;
		directory->current_name_length = length;
		*kind = find_entry_kind(&directory->current);
		*name_length = length;
		return 1;
	}
}

int dynlex_filesystem_directory_copy_name(const DynlexWindowsDirectory *directory, char *buffer, size_t capacity) {
	dynlex_runtime_clear_error();
	if (directory == NULL || directory->current_name == NULL || buffer == NULL || capacity <= directory->current_name_length) {
		dynlex_runtime_set_error("Directory entry name buffer is too small");
		return -1;
	}
	memcpy(buffer, directory->current_name, directory->current_name_length + 1);
	return 0;
}

void dynlex_platform_filesystem_directory_destroy(void *opaque_directory) {
	DynlexWindowsDirectory *directory = opaque_directory;
	FindClose(directory->search);
	free(directory->current_name);
	free(directory);
}

static int ensure_directory(const wchar_t *path) {
	DWORD attributes = GetFileAttributesW(path);
	if (attributes != INVALID_FILE_ATTRIBUTES) {
		if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0)
			return 0;
		dynlex_runtime_set_error("Filesystem path component is not a directory");
		return -1;
	}
	DWORD error_number = GetLastError();
	if (!missing_error(error_number)) {
		dynlex_runtime_set_windows_error("Could not inspect directory path", error_number);
		return -1;
	}
	if (CreateDirectoryW(path, NULL))
		return 0;
	error_number = GetLastError();
	attributes = GetFileAttributesW(path);
	if (error_number == ERROR_ALREADY_EXISTS && attributes != INVALID_FILE_ATTRIBUTES &&
		(attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0)
		return 0;
	dynlex_runtime_set_windows_error("Could not create directory", error_number);
	return -1;
}

int dynlex_filesystem_create_directories(const char *path, size_t path_length) {
	dynlex_runtime_clear_error();
	wchar_t *prepared = dynlex_platform_filesystem_wide_path(path, path_length);
	if (prepared == NULL)
		return -1;
	trim_trailing_separators(prepared);
	size_t length = wcslen(prepared);
	size_t root = root_length(prepared);
	for (size_t index = root; index < length; ++index) {
		if (prepared[index] != L'/' && prepared[index] != L'\\')
			continue;
		if (index > root && (prepared[index - 1] == L'/' || prepared[index - 1] == L'\\'))
			continue;
		prepared[index] = L'\0';
		if (prepared[0] != L'\0' && ensure_directory(prepared) != 0) {
			free(prepared);
			return -1;
		}
		prepared[index] = L'\\';
	}
	int result = ensure_directory(prepared);
	free(prepared);
	return result;
}

static int remove_tree_wide(const wchar_t *path) {
	DWORD attributes = GetFileAttributesW(path);
	if (attributes == INVALID_FILE_ATTRIBUTES) {
		DWORD error_number = GetLastError();
		if (missing_error(error_number))
			return 0;
		dynlex_runtime_set_windows_error("Could not inspect cleanup path", error_number);
		return -1;
	}
	if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0 || (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
		BOOL removed = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ? RemoveDirectoryW(path) : DeleteFileW(path);
		if (!removed) {
			dynlex_runtime_set_windows_error("Could not remove filesystem entry", GetLastError());
			return -1;
		}
		return 0;
	}
	size_t length = wcslen(path);
	if (length > SIZE_MAX - MAX_PATH - 3 || length + MAX_PATH + 3 > SIZE_MAX / sizeof(wchar_t)) {
		dynlex_runtime_set_error("Cleanup path is too large");
		return -1;
	}
	wchar_t *child = malloc((length + MAX_PATH + 3) * sizeof(wchar_t));
	if (child == NULL) {
		dynlex_runtime_set_errno_error("Could not allocate cleanup path", ENOMEM);
		return -1;
	}
	memcpy(child, path, length * sizeof(wchar_t));
	if (length > 0 && child[length - 1] != L'/' && child[length - 1] != L'\\')
		child[length++] = L'\\';
	child[length] = L'*';
	child[length + 1] = L'\0';
	WIN32_FIND_DATAW entry;
	HANDLE search = FindFirstFileW(child, &entry);
	if (search == INVALID_HANDLE_VALUE) {
		DWORD error_number = GetLastError();
		free(child);
		if (error_number != ERROR_FILE_NOT_FOUND) {
			dynlex_runtime_set_windows_error("Could not enumerate cleanup directory", error_number);
			return -1;
		}
	} else {
		int result = 0;
		do {
			if (wcscmp(entry.cFileName, L".") == 0 || wcscmp(entry.cFileName, L"..") == 0)
				continue;
			size_t name_length = wcslen(entry.cFileName);
			if (length > SIZE_MAX - name_length - 1 || length + name_length + 1 > SIZE_MAX / sizeof(wchar_t)) {
				dynlex_runtime_set_error("Cleanup path is too large");
				result = -1;
				break;
			}
			wchar_t *resized = realloc(child, (length + name_length + 1) * sizeof(wchar_t));
			if (resized == NULL) {
				dynlex_runtime_set_errno_error("Could not allocate cleanup path", ENOMEM);
				result = -1;
				break;
			}
			child = resized;
			memcpy(child + length, entry.cFileName, (name_length + 1) * sizeof(wchar_t));
			if (remove_tree_wide(child) != 0) {
				result = -1;
				break;
			}
		} while (FindNextFileW(search, &entry));
		DWORD enumeration_error = GetLastError();
		FindClose(search);
		free(child);
		if (result != 0)
			return -1;
		if (enumeration_error != ERROR_NO_MORE_FILES) {
			dynlex_runtime_set_windows_error("Could not enumerate cleanup directory", enumeration_error);
			return -1;
		}
	}
	if (!RemoveDirectoryW(path)) {
		dynlex_runtime_set_windows_error("Could not remove cleanup directory", GetLastError());
		return -1;
	}
	return 0;
}

int dynlex_platform_filesystem_remove_tree(const char *path, size_t path_length) {
	wchar_t *prepared = dynlex_platform_filesystem_wide_path(path, path_length);
	if (prepared == NULL)
		return -1;
	trim_trailing_separators(prepared);
	int result = remove_tree_wide(prepared);
	free(prepared);
	return result;
}

int dynlex_filesystem_remove_tree(const char *path, size_t path_length) {
	dynlex_runtime_clear_error();
	return dynlex_platform_filesystem_remove_tree(path, path_length);
}

int dynlex_filesystem_rename(const char *source, size_t source_length, const char *destination, size_t destination_length) {
	dynlex_runtime_clear_error();
	wchar_t *prepared_source = dynlex_platform_filesystem_wide_path(source, source_length);
	if (prepared_source == NULL)
		return -1;
	wchar_t *prepared_destination = dynlex_platform_filesystem_wide_path(destination, destination_length);
	if (prepared_destination == NULL) {
		free(prepared_source);
		return -1;
	}
	BOOL moved = MoveFileExW(prepared_source, prepared_destination, MOVEFILE_REPLACE_EXISTING);
	DWORD error_number = GetLastError();
	free(prepared_source);
	free(prepared_destination);
	if (!moved) {
		dynlex_runtime_set_windows_error("Could not rename filesystem entry", error_number);
		return -1;
	}
	return 0;
}

int dynlex_platform_filesystem_create_temporary_directory(char **path, size_t *length) {
	DWORD required = GetTempPathW(0, NULL);
	if (required == 0) {
		dynlex_runtime_set_windows_error("Could not find host temporary directory", GetLastError());
		return -1;
	}
	wchar_t *base = malloc(((size_t)required + 1) * sizeof(wchar_t));
	if (base == NULL) {
		dynlex_runtime_set_errno_error("Could not allocate host temporary directory path", ENOMEM);
		return -1;
	}
	DWORD actual = GetTempPathW(required + 1, base);
	if (actual == 0 || actual > required) {
		DWORD error_number = GetLastError();
		free(base);
		dynlex_runtime_set_windows_error("Could not find host temporary directory", error_number);
		return -1;
	}
	DWORD absolute_required = GetFullPathNameW(base, 0, NULL, NULL);
	if (absolute_required == 0) {
		DWORD error_number = GetLastError();
		free(base);
		dynlex_runtime_set_windows_error("Could not resolve host temporary directory", error_number);
		return -1;
	}
	wchar_t *absolute_base = malloc((size_t)absolute_required * sizeof(wchar_t));
	if (absolute_base == NULL) {
		free(base);
		dynlex_runtime_set_errno_error("Could not allocate absolute host temporary directory path", ENOMEM);
		return -1;
	}
	if (GetFullPathNameW(base, absolute_required, absolute_base, NULL) == 0) {
		DWORD error_number = GetLastError();
		free(absolute_base);
		free(base);
		dynlex_runtime_set_windows_error("Could not resolve host temporary directory", error_number);
		return -1;
	}
	free(base);
	base = absolute_base;
	size_t base_length = wcslen(base);
	if (base_length > SIZE_MAX - 81 || base_length + 81 > SIZE_MAX / sizeof(wchar_t)) {
		free(base);
		dynlex_runtime_set_error("Host temporary directory path is too large");
		return -1;
	}
	size_t candidate_capacity = base_length + 81;
	const wchar_t *separator =
		base_length > 0 && (base[base_length - 1] == L'/' || base[base_length - 1] == L'\\') ? L"" : L"\\";
	wchar_t *candidate = malloc(candidate_capacity * sizeof(wchar_t));
	if (candidate == NULL) {
		free(base);
		dynlex_runtime_set_errno_error("Could not allocate temporary directory path", ENOMEM);
		return -1;
	}
	for (DWORD attempt = 0; attempt < 1024; ++attempt) {
		LARGE_INTEGER counter;
		QueryPerformanceCounter(&counter);
		int count = swprintf(
			candidate, candidate_capacity, L"%ls%lsdynlex-%08lx-%016llx-%08lx", base, separator, GetCurrentProcessId(),
			(unsigned long long)counter.QuadPart, attempt
		);
		if (count < 0 || (size_t)count >= candidate_capacity) {
			free(candidate);
			free(base);
			dynlex_runtime_set_error("Temporary directory path is too large");
			return -1;
		}
		if (CreateDirectoryW(candidate, NULL)) {
			free(base);
			*path = dynlex_platform_filesystem_utf8_text(candidate, length);
			if (*path != NULL) {
				free(candidate);
				return 0;
			}
			RemoveDirectoryW(candidate);
			free(candidate);
			return -1;
		}
		DWORD error_number = GetLastError();
		if (error_number != ERROR_ALREADY_EXISTS) {
			free(candidate);
			free(base);
			dynlex_runtime_set_windows_error("Could not create temporary directory", error_number);
			return -1;
		}
	}
	free(candidate);
	free(base);
	dynlex_runtime_set_error("Could not create a unique temporary directory");
	return -1;
}
