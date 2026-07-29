#define WIN32_LEAN_AND_MEAN

#include "processRuntimeInternal.h"
#include "processRuntimeWindowsQuoting.h"

#include "runtimeError.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>

typedef struct DynlexWindowsProcess DynlexWindowsProcess;

typedef struct {
	DynlexWindowsProcess *platform;
	DynlexProcessStream stream;
	HANDLE pipe;
} DynlexWindowsReader;

struct DynlexWindowsProcess {
	DynlexProcess *owner;
	HANDLE process;
	HANDLE standard_input;
	HANDLE standard_output;
	HANDLE standard_error;
	HANDLE output_thread;
	HANDLE error_thread;
	HANDLE activity_event;
	CRITICAL_SECTION output_lock;
	DynlexWindowsReader output_reader;
	DynlexWindowsReader error_reader;
	volatile LONG stop_readers;
	bool io_error;
	DWORD io_error_code;
};

typedef struct {
	wchar_t *name;
	wchar_t *entry;
	bool unset;
} DynlexWindowsEnvironmentOverride;

static wchar_t *utf8_to_wide(const char *text, size_t length, const char *operation) {
	if (length > INT_MAX) {
		dynlex_runtime_set_error("Process text exceeds the Windows conversion limit");
		return NULL;
	}
	int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, (int)length, NULL, 0);
	if (required == 0 && length != 0) {
		dynlex_runtime_set_windows_error(operation, GetLastError());
		return NULL;
	}
	wchar_t *wide = calloc((size_t)required + 1, sizeof(*wide));
	if (wide == NULL) {
		dynlex_runtime_set_errno_error("Could not allocate Windows process text", ENOMEM);
		return NULL;
	}
	if (required > 0 && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, (int)length, wide, required) != required) {
		DWORD error_number = GetLastError();
		free(wide);
		dynlex_runtime_set_windows_error(operation, error_number);
		return NULL;
	}
	return wide;
}

static int
compare_ordinal_case_insensitive(const wchar_t *left, size_t left_length, const wchar_t *right, size_t right_length) {
	if (left_length > INT_MAX || right_length > INT_MAX)
		abort();
	int result = CompareStringOrdinal(left, (int)left_length, right, (int)right_length, TRUE);
	if (result == 0)
		abort();
	return result - CSTR_EQUAL;
}

static bool wide_environment_name_matches(const wchar_t *entry, const wchar_t *name) {
	const wchar_t *separator = wcschr(entry, L'=');
	if (separator == NULL)
		return false;
	return compare_ordinal_case_insensitive(entry, (size_t)(separator - entry), name, wcslen(name)) == 0;
}

static void free_environment_overrides(DynlexWindowsEnvironmentOverride *overrides, size_t override_count) {
	for (size_t index = 0; index < override_count; ++index) {
		free(overrides[index].name);
		free(overrides[index].entry);
	}
	free(overrides);
}

static DynlexWindowsEnvironmentOverride *
build_environment_overrides(const DynlexProcessCommand *command, size_t *result_count) {
	DynlexWindowsEnvironmentOverride *overrides = calloc(command->environment_count, sizeof(*overrides));
	if (overrides == NULL && command->environment_count != 0) {
		dynlex_runtime_set_errno_error("Could not allocate Windows environment overrides", ENOMEM);
		return NULL;
	}
	size_t override_count = 0;
	for (size_t index = 0; index < command->environment_count; ++index) {
		const DynlexProcessEnvironmentEntry *source = &command->environment[index];
		DynlexWindowsEnvironmentOverride converted = {0};
		converted.name = utf8_to_wide(source->name.data, source->name.length, "Invalid environment variable name");
		if (converted.name == NULL)
			goto failure;
		converted.unset = source->unset;
		wchar_t *value = NULL;
		if (!source->unset)
			value = utf8_to_wide(source->value.data, source->value.length, "Invalid environment variable value");
		if (!source->unset && value == NULL) {
			free(converted.name);
			goto failure;
		}
		size_t name_length = wcslen(converted.name);
		size_t value_length = value == NULL ? 0 : wcslen(value);
		if (!source->unset &&
			(name_length > SIZE_MAX - value_length - 2 || name_length + value_length + 2 > SIZE_MAX / sizeof(wchar_t))) {
			free(value);
			free(converted.name);
			dynlex_runtime_set_error("Windows environment entry is too large");
			goto failure;
		}
		if (!source->unset)
			converted.entry = malloc((name_length + value_length + 2) * sizeof(wchar_t));
		if (!source->unset && converted.entry == NULL) {
			free(value);
			free(converted.name);
			dynlex_runtime_set_errno_error("Could not allocate Windows environment entry", ENOMEM);
			goto failure;
		}
		if (!source->unset) {
			memcpy(converted.entry, converted.name, name_length * sizeof(wchar_t));
			converted.entry[name_length] = L'=';
			memcpy(converted.entry + name_length + 1, value, (value_length + 1) * sizeof(wchar_t));
		}
		free(value);
		size_t destination = override_count;
		for (size_t candidate = 0; candidate < override_count; ++candidate) {
			if (compare_ordinal_case_insensitive(
					overrides[candidate].name, wcslen(overrides[candidate].name), converted.name, name_length
				) == 0) {
				destination = candidate;
				break;
			}
		}
		if (destination == override_count)
			override_count++;
		else {
			free(overrides[destination].name);
			free(overrides[destination].entry);
		}
		overrides[destination] = converted;
	}
	*result_count = override_count;
	return overrides;

failure:
	free_environment_overrides(overrides, override_count);
	return NULL;
}

static bool inherited_environment_is_overridden(
	const wchar_t *entry, const DynlexWindowsEnvironmentOverride *overrides, size_t override_count
) {
	if (entry[0] == L'=')
		return false;
	for (size_t index = 0; index < override_count; ++index) {
		if (wide_environment_name_matches(entry, overrides[index].name))
			return true;
	}
	return false;
}

static int compare_environment_entries(const void *left, const void *right) {
	const wchar_t *const *left_entry = left;
	const wchar_t *const *right_entry = right;
	return compare_ordinal_case_insensitive(*left_entry, wcslen(*left_entry), *right_entry, wcslen(*right_entry));
}

static wchar_t *build_environment_block(const DynlexProcessCommand *command) {
	size_t configured_override_count = 0;
	DynlexWindowsEnvironmentOverride *overrides = build_environment_overrides(command, &configured_override_count);
	if (overrides == NULL && command->environment_count != 0)
		return NULL;
	LPWCH inherited = command->inherit_environment ? GetEnvironmentStringsW() : NULL;
	if (command->inherit_environment && inherited == NULL) {
		free_environment_overrides(overrides, configured_override_count);
		dynlex_runtime_set_windows_error("Could not read inherited environment", GetLastError());
		return NULL;
	}
	size_t inherited_count = 0;
	for (const wchar_t *entry = inherited; entry != NULL && *entry != L'\0'; entry += wcslen(entry) + 1) {
		if (!inherited_environment_is_overridden(entry, overrides, configured_override_count))
			inherited_count++;
	}
	size_t active_override_count = 0;
	for (size_t index = 0; index < configured_override_count; ++index) {
		if (!overrides[index].unset)
			active_override_count++;
	}
	if (inherited_count > SIZE_MAX - active_override_count) {
		dynlex_runtime_set_error("Windows process environment contains too many entries");
		goto failure;
	}
	size_t entry_count = inherited_count + active_override_count;
	wchar_t **entries = calloc(entry_count, sizeof(*entries));
	if (entries == NULL && entry_count != 0) {
		dynlex_runtime_set_errno_error("Could not allocate Windows process environment", ENOMEM);
		goto failure;
	}
	size_t count = 0;
	for (wchar_t *entry = inherited; entry != NULL && *entry != L'\0'; entry += wcslen(entry) + 1) {
		if (!inherited_environment_is_overridden(entry, overrides, configured_override_count))
			entries[count++] = entry;
	}
	for (size_t index = 0; index < configured_override_count; ++index) {
		if (!overrides[index].unset)
			entries[count++] = overrides[index].entry;
	}
	if (entry_count > 1)
		qsort(entries, entry_count, sizeof(*entries), compare_environment_entries);
	size_t total = entry_count == 0 ? 2 : 1;
	for (size_t index = 0; index < entry_count; ++index) {
		size_t entry_length = wcslen(entries[index]) + 1;
		if (total > SIZE_MAX - entry_length) {
			free(entries);
			dynlex_runtime_set_error("Windows process environment is too large");
			goto failure;
		}
		total += entry_length;
	}
	wchar_t *block = calloc(total, sizeof(*block));
	if (block == NULL) {
		free(entries);
		dynlex_runtime_set_errno_error("Could not allocate Windows process environment block", ENOMEM);
		goto failure;
	}
	size_t offset = 0;
	for (size_t index = 0; index < entry_count; ++index) {
		size_t entry_length = wcslen(entries[index]) + 1;
		memcpy(block + offset, entries[index], entry_length * sizeof(wchar_t));
		offset += entry_length;
	}
	block[offset] = L'\0';
	free(entries);
	if (inherited != NULL)
		FreeEnvironmentStringsW(inherited);
	free_environment_overrides(overrides, configured_override_count);
	return block;

failure:
	if (inherited != NULL)
		FreeEnvironmentStringsW(inherited);
	free_environment_overrides(overrides, configured_override_count);
	return NULL;
}

static const wchar_t *environment_block_value(const wchar_t *environment, const wchar_t *name) {
	size_t name_length = wcslen(name);
	for (const wchar_t *entry = environment; *entry != L'\0'; entry += wcslen(entry) + 1) {
		if (entry[0] == L'=')
			continue;
		const wchar_t *separator = wcschr(entry, L'=');
		if (separator != NULL && compare_ordinal_case_insensitive(entry, (size_t)(separator - entry), name, name_length) == 0)
			return separator + 1;
	}
	return NULL;
}

static wchar_t *absolute_path(const wchar_t *path, const char *operation) {
	DWORD capacity = GetFullPathNameW(path, 0, NULL, NULL);
	if (capacity == 0) {
		dynlex_runtime_set_windows_error(operation, GetLastError());
		return NULL;
	}
	while (true) {
		wchar_t *absolute = calloc(capacity, sizeof(*absolute));
		if (absolute == NULL) {
			dynlex_runtime_set_errno_error("Could not allocate absolute Windows path", ENOMEM);
			return NULL;
		}
		DWORD length = GetFullPathNameW(path, capacity, absolute, NULL);
		if (length == 0) {
			DWORD error_number = GetLastError();
			free(absolute);
			dynlex_runtime_set_windows_error(operation, error_number);
			return NULL;
		}
		if (length < capacity)
			return absolute;
		free(absolute);
		capacity = length;
	}
}

static bool is_path_separator(wchar_t character) { return character == L'\\' || character == L'/'; }

static bool path_has_drive_designator(const wchar_t *path) { return path[0] != L'\0' && path[1] == L':'; }

static bool path_is_fully_qualified(const wchar_t *path) {
	return (is_path_separator(path[0]) && is_path_separator(path[1])) ||
		   (path_has_drive_designator(path) && is_path_separator(path[2]));
}

static bool reject_ambiguous_windows_path(const wchar_t *path, bool executable) {
	bool root_relative = is_path_separator(path[0]) && !is_path_separator(path[1]);
	bool drive_relative = path_has_drive_designator(path) && !is_path_separator(path[2]);
	if (!root_relative && !drive_relative)
		return false;
	if (root_relative)
		dynlex_runtime_set_error(
			executable ? "Windows executable paths must not be relative to the current drive root"
					   : "Windows PATH entries must not be relative to the current drive root"
		);
	else
		dynlex_runtime_set_error(
			executable ? "Windows executable paths must not use drive-relative syntax"
					   : "Windows PATH entries must not use drive-relative syntax"
		);
	return true;
}

static wchar_t *copy_wide_segment(const wchar_t *start, size_t length) {
	if (length > (SIZE_MAX / sizeof(wchar_t)) - 1) {
		dynlex_runtime_set_error("Windows path entry is too large");
		return NULL;
	}
	wchar_t *copy = malloc((length + 1) * sizeof(*copy));
	if (copy == NULL) {
		dynlex_runtime_set_errno_error("Could not allocate Windows path entry", ENOMEM);
		return NULL;
	}
	if (length > 0)
		memcpy(copy, start, length * sizeof(*copy));
	copy[length] = L'\0';
	return copy;
}

static wchar_t *path_relative_to_directory(const wchar_t *path, const wchar_t *working_directory) {
	if (reject_ambiguous_windows_path(path, false))
		return NULL;
	if (path_is_fully_qualified(path) || working_directory == NULL)
		return absolute_path(path[0] == L'\0' ? L"." : path, "Could not resolve Windows PATH entry");
	size_t directory_length = wcslen(working_directory);
	size_t path_length = wcslen(path);
	if (directory_length > SIZE_MAX - path_length - 2 || directory_length + path_length + 2 > SIZE_MAX / sizeof(wchar_t)) {
		dynlex_runtime_set_error("Windows PATH entry is too large");
		return NULL;
	}
	wchar_t *combined = malloc((directory_length + path_length + 2) * sizeof(*combined));
	if (combined == NULL) {
		dynlex_runtime_set_errno_error("Could not allocate Windows PATH entry", ENOMEM);
		return NULL;
	}
	memcpy(combined, working_directory, directory_length * sizeof(*combined));
	combined[directory_length] = L'\\';
	memcpy(combined + directory_length + 1, path, (path_length + 1) * sizeof(*combined));
	wchar_t *absolute = absolute_path(combined, "Could not resolve Windows PATH entry");
	free(combined);
	return absolute;
}

static wchar_t *search_executable_path(const wchar_t *executable, const wchar_t *path, const wchar_t *working_directory) {
	DWORD last_error = ERROR_FILE_NOT_FOUND;
	const wchar_t *entry = path;
	while (true) {
		const wchar_t *separator = wcschr(entry, L';');
		size_t length = separator == NULL ? wcslen(entry) : (size_t)(separator - entry);
		const wchar_t *content = entry;
		if (length >= 2 && content[0] == L'"' && content[length - 1] == L'"') {
			content++;
			length -= 2;
		}
		wchar_t *component = copy_wide_segment(content, length);
		if (component == NULL)
			return NULL;
		wchar_t *directory = path_relative_to_directory(component, working_directory);
		free(component);
		if (directory == NULL)
			return NULL;

		DWORD capacity = SearchPathW(directory, executable, L".exe", 0, NULL, NULL);
		if (capacity != 0) {
			wchar_t *resolved = calloc((size_t)capacity + 1, sizeof(*resolved));
			if (resolved == NULL) {
				free(directory);
				dynlex_runtime_set_errno_error("Could not allocate resolved Windows executable path", ENOMEM);
				return NULL;
			}
			DWORD copied = SearchPathW(directory, executable, L".exe", capacity + 1, resolved, NULL);
			free(directory);
			if (copied != 0 && copied <= capacity)
				return resolved;
			last_error = GetLastError();
			free(resolved);
			if (last_error != ERROR_FILE_NOT_FOUND && last_error != ERROR_PATH_NOT_FOUND)
				break;
		} else {
			last_error = GetLastError();
			free(directory);
			if (last_error != ERROR_FILE_NOT_FOUND && last_error != ERROR_PATH_NOT_FOUND)
				break;
		}
		if (separator == NULL)
			break;
		entry = separator + 1;
	}
	dynlex_runtime_set_windows_error("Could not resolve process executable", last_error);
	return NULL;
}

static wchar_t *build_command_line(const DynlexProcessCommand *command, const wchar_t *wide_executable) {
	wchar_t **arguments = calloc(command->argument_count + 1, sizeof(*arguments));
	if (arguments == NULL) {
		dynlex_runtime_set_errno_error("Could not allocate Windows argument conversion", ENOMEM);
		return NULL;
	}
	arguments[0] = _wcsdup(wide_executable);
	if (arguments[0] == NULL) {
		dynlex_runtime_set_errno_error("Could not allocate Windows executable argument", ENOMEM);
		free(arguments);
		return NULL;
	}
	size_t total = dynlex_windows_quoted_argument_length(arguments[0]) + 1;
	for (size_t index = 0; index < command->argument_count; ++index) {
		arguments[index + 1] =
			utf8_to_wide(command->arguments[index].data, command->arguments[index].length, "Invalid Windows process argument");
		if (arguments[index + 1] == NULL)
			goto failure;
		size_t length = dynlex_windows_quoted_argument_length(arguments[index + 1]);
		if (total > SIZE_MAX - length - 1) {
			dynlex_runtime_set_error("Windows command line is too large");
			goto failure;
		}
		total += length + 1;
	}
	wchar_t *command_line = calloc(total, sizeof(*command_line));
	if (command_line == NULL) {
		dynlex_runtime_set_errno_error("Could not allocate Windows command line", ENOMEM);
		goto failure;
	}
	wchar_t *destination = command_line;
	for (size_t index = 0; index <= command->argument_count; ++index) {
		if (index != 0)
			*destination++ = L' ';
		destination = dynlex_windows_append_quoted_argument(destination, arguments[index]);
	}
	*destination = L'\0';
	for (size_t index = 0; index <= command->argument_count; ++index)
		free(arguments[index]);
	free(arguments);
	return command_line;

failure:
	for (size_t index = 0; index <= command->argument_count; ++index)
		free(arguments[index]);
	free(arguments);
	return NULL;
}

static wchar_t *
resolve_executable(const DynlexProcessCommand *command, const wchar_t *working_directory, const wchar_t *environment) {
	wchar_t *executable = utf8_to_wide(command->executable.data, command->executable.length, "Invalid Windows executable");
	if (executable == NULL)
		return NULL;
	if (reject_ambiguous_windows_path(executable, true)) {
		free(executable);
		return NULL;
	}
	bool has_separator = wcschr(executable, L'\\') != NULL || wcschr(executable, L'/') != NULL;
	if (!has_separator) {
		const wchar_t *path = environment_block_value(environment, L"PATH");
		if (path == NULL) {
			free(executable);
			dynlex_runtime_set_error("Could not resolve process executable because PATH is not set");
			return NULL;
		}
		wchar_t *resolved = search_executable_path(executable, path, working_directory);
		free(executable);
		return resolved;
	}
	wchar_t *resolved = path_relative_to_directory(executable, working_directory);
	free(executable);
	return resolved;
}

static int create_inherited_pipe(HANDLE *read_handle, HANDLE *write_handle) {
	SECURITY_ATTRIBUTES security = {
		.nLength = sizeof(security),
		.bInheritHandle = TRUE,
	};
	if (!CreatePipe(read_handle, write_handle, &security, 0))
		return -1;
	return 0;
}

static DWORD WINAPI reader_thread(void *argument) {
	DynlexWindowsReader *reader = argument;
	DynlexWindowsProcess *platform = reader->platform;
	char chunk[4096];
	while (true) {
		if (InterlockedCompareExchange(&platform->stop_readers, 0, 0) != 0)
			break;
		DWORD count = 0;
		if (ReadFile(reader->pipe, chunk, sizeof(chunk), &count, NULL)) {
			if (count == 0)
				break;
			EnterCriticalSection(&platform->output_lock);
			int append_result = dynlex_process_append_output(platform->owner, reader->stream, chunk, count);
			if (append_result != 0 && !platform->io_error) {
				platform->io_error = true;
				platform->io_error_code = 0;
			}
			LeaveCriticalSection(&platform->output_lock);
			SetEvent(platform->activity_event);
			if (append_result != 0)
				break;
			continue;
		}
		DWORD error_number = GetLastError();
		if (error_number != ERROR_BROKEN_PIPE && error_number != ERROR_OPERATION_ABORTED) {
			EnterCriticalSection(&platform->output_lock);
			if (!platform->io_error) {
				platform->io_error = true;
				platform->io_error_code = error_number;
			}
			LeaveCriticalSection(&platform->output_lock);
		}
		break;
	}
	EnterCriticalSection(&platform->output_lock);
	dynlex_process_mark_stream_closed(platform->owner, reader->stream);
	LeaveCriticalSection(&platform->output_lock);
	SetEvent(platform->activity_event);
	return 0;
}

static void close_handle(HANDLE *handle) {
	if (*handle != NULL && *handle != INVALID_HANDLE_VALUE)
		CloseHandle(*handle);
	*handle = NULL;
}

int dynlex_platform_process_launch(DynlexProcess *process, const DynlexProcessCommand *command) {
	DynlexWindowsProcess *platform = calloc(1, sizeof(*platform));
	wchar_t *working_directory = NULL;
	wchar_t *executable = NULL;
	wchar_t *command_line = NULL;
	wchar_t *environment = NULL;
	HANDLE child_input = NULL;
	HANDLE child_output = NULL;
	HANDLE child_error = NULL;
	PPROC_THREAD_ATTRIBUTE_LIST attributes = NULL;
	bool attributes_initialized = false;
	PROCESS_INFORMATION process_information = {0};
	if (platform == NULL) {
		dynlex_runtime_set_errno_error("Could not allocate Windows process", ENOMEM);
		return -1;
	}
	InitializeCriticalSection(&platform->output_lock);
	platform->activity_event = CreateEventW(NULL, FALSE, FALSE, NULL);
	if (platform->activity_event == NULL) {
		dynlex_runtime_set_windows_error("Could not create Windows process activity event", GetLastError());
		goto failure;
	}
	platform->owner = process;
	platform->output_reader = (DynlexWindowsReader){platform, DYNLEX_PROCESS_STREAM_STDOUT, NULL};
	platform->error_reader = (DynlexWindowsReader){platform, DYNLEX_PROCESS_STREAM_STDERR, NULL};

	if (command->has_working_directory) {
		wchar_t *requested_directory = utf8_to_wide(
			command->working_directory.data, command->working_directory.length, "Invalid Windows working directory"
		);
		if (requested_directory == NULL)
			goto failure;
		working_directory = absolute_path(requested_directory, "Could not resolve Windows working directory");
		free(requested_directory);
		if (working_directory == NULL)
			goto failure;
	}
	environment = build_environment_block(command);
	if (environment == NULL)
		goto failure;
	executable = resolve_executable(command, working_directory, environment);
	if (executable == NULL)
		goto failure;
	command_line = build_command_line(command, executable);
	if (command_line == NULL)
		goto failure;

	if (create_inherited_pipe(&child_input, &platform->standard_input) != 0 ||
		create_inherited_pipe(&platform->standard_output, &child_output) != 0 ||
		create_inherited_pipe(&platform->standard_error, &child_error) != 0) {
		dynlex_runtime_set_windows_error("Could not create Windows process pipes", GetLastError());
		goto failure;
	}
	if (!SetHandleInformation(platform->standard_input, HANDLE_FLAG_INHERIT, 0) ||
		!SetHandleInformation(platform->standard_output, HANDLE_FLAG_INHERIT, 0) ||
		!SetHandleInformation(platform->standard_error, HANDLE_FLAG_INHERIT, 0)) {
		dynlex_runtime_set_windows_error("Could not configure Windows process pipes", GetLastError());
		goto failure;
	}

	SIZE_T attribute_size = 0;
	(void)InitializeProcThreadAttributeList(NULL, 1, 0, &attribute_size);
	attributes = HeapAlloc(GetProcessHeap(), 0, attribute_size);
	if (attributes == NULL) {
		dynlex_runtime_set_errno_error("Could not allocate Windows process attributes", ENOMEM);
		goto failure;
	}
	if (!InitializeProcThreadAttributeList(attributes, 1, 0, &attribute_size)) {
		dynlex_runtime_set_windows_error("Could not initialize Windows process attributes", GetLastError());
		goto failure;
	}
	attributes_initialized = true;
	HANDLE inherited_handles[3] = {child_input, child_output, child_error};
	if (!UpdateProcThreadAttribute(
			attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited_handles, sizeof(inherited_handles), NULL, NULL
		)) {
		dynlex_runtime_set_windows_error("Could not restrict inherited Windows process handles", GetLastError());
		goto failure;
	}
	STARTUPINFOEXW startup = {0};
	startup.StartupInfo.cb = sizeof(startup);
	startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
	startup.StartupInfo.hStdInput = child_input;
	startup.StartupInfo.hStdOutput = child_output;
	startup.StartupInfo.hStdError = child_error;
	startup.lpAttributeList = attributes;
	if (!CreateProcessW(
			executable, command_line, NULL, NULL, TRUE, CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT, environment,
			working_directory, &startup.StartupInfo, &process_information
		)) {
		dynlex_runtime_set_windows_error("Could not launch process", GetLastError());
		goto failure;
	}
	platform->process = process_information.hProcess;
	CloseHandle(process_information.hThread);
	process_information.hThread = NULL;
	close_handle(&child_input);
	close_handle(&child_output);
	close_handle(&child_error);
	platform->output_reader.pipe = platform->standard_output;
	platform->error_reader.pipe = platform->standard_error;
	platform->output_thread = CreateThread(NULL, 0, reader_thread, &platform->output_reader, 0, NULL);
	DWORD reader_error = platform->output_thread == NULL ? GetLastError() : ERROR_SUCCESS;
	if (platform->output_thread != NULL)
		platform->error_thread = CreateThread(NULL, 0, reader_thread, &platform->error_reader, 0, NULL);
	if (platform->error_thread == NULL && reader_error == ERROR_SUCCESS)
		reader_error = GetLastError();
	if (platform->output_thread == NULL || platform->error_thread == NULL) {
		dynlex_runtime_set_windows_error("Could not start Windows process pipe readers", reader_error);
		TerminateProcess(platform->process, 1);
		WaitForSingleObject(platform->process, INFINITE);
		goto failure;
	}
	process->platform = platform;
	DeleteProcThreadAttributeList(attributes);
	attributes_initialized = false;
	HeapFree(GetProcessHeap(), 0, attributes);
	free(working_directory);
	free(executable);
	free(command_line);
	free(environment);
	return 0;

failure:
	if (attributes != NULL) {
		if (attributes_initialized)
			DeleteProcThreadAttributeList(attributes);
		HeapFree(GetProcessHeap(), 0, attributes);
	}
	close_handle(&child_input);
	close_handle(&child_output);
	close_handle(&child_error);
	close_handle(&platform->standard_input);
	if (platform->output_thread != NULL) {
		InterlockedExchange(&platform->stop_readers, 1);
		CancelSynchronousIo(platform->output_thread);
		WaitForSingleObject(platform->output_thread, INFINITE);
		close_handle(&platform->output_thread);
	}
	if (platform->error_thread != NULL) {
		CancelSynchronousIo(platform->error_thread);
		WaitForSingleObject(platform->error_thread, INFINITE);
		close_handle(&platform->error_thread);
	}
	close_handle(&platform->standard_output);
	close_handle(&platform->standard_error);
	close_handle(&platform->activity_event);
	close_handle(&platform->process);
	DeleteCriticalSection(&platform->output_lock);
	free(platform);
	free(working_directory);
	free(executable);
	free(command_line);
	free(environment);
	return -1;
}

static bool requested_stream_ready(DynlexProcess *process, DynlexProcessStream stream) {
	if (stream == 0)
		return false;
	if (stream == DYNLEX_PROCESS_STREAM_ANY)
		return process->standard_output.length > 0 || process->standard_error.length > 0 ||
			   (process->standard_output_closed && process->standard_error_closed);
	DynlexProcessBuffer *buffer = stream == DYNLEX_PROCESS_STREAM_STDOUT ? &process->standard_output : &process->standard_error;
	bool closed = stream == DYNLEX_PROCESS_STREAM_STDOUT ? process->standard_output_closed : process->standard_error_closed;
	return buffer->length > 0 || closed;
}

static int report_reader_error(DynlexWindowsProcess *platform) {
	EnterCriticalSection(&platform->output_lock);
	bool has_error = platform->io_error;
	DWORD error_code = platform->io_error_code;
	LeaveCriticalSection(&platform->output_lock);
	if (!has_error)
		return 0;
	if (error_code != 0)
		dynlex_runtime_set_windows_error("Could not read Windows process output", error_code);
	else
		dynlex_runtime_set_error("Could not store captured Windows process output");
	return -1;
}

static void stop_reader(DynlexWindowsProcess *platform, HANDLE *thread) {
	InterlockedExchange(&platform->stop_readers, 1);
	if (*thread != NULL) {
		CancelSynchronousIo(*thread);
		WaitForSingleObject(*thread, INFINITE);
		close_handle(thread);
	}
}

static void cancel_reader(DynlexWindowsProcess *platform, HANDLE *thread, HANDLE *pipe) {
	stop_reader(platform, thread);
	close_handle(pipe);
}

static int capture_buffered_pipe_after_exit(DynlexProcess *process, DynlexProcessStream stream, HANDLE *pipe) {
	if (*pipe == NULL)
		return 0;
	DWORD available = 0;
	if (!PeekNamedPipe(*pipe, NULL, 0, NULL, &available, NULL)) {
		DWORD error_number = GetLastError();
		if (error_number == ERROR_BROKEN_PIPE)
			return 0;
		dynlex_runtime_set_windows_error("Could not inspect buffered Windows process output", error_number);
		return -1;
	}
	DWORD remaining = available;
	char chunk[4096];
	while (remaining > 0) {
		DWORD requested = remaining < sizeof(chunk) ? remaining : (DWORD)sizeof(chunk);
		DWORD count = 0;
		if (!ReadFile(*pipe, chunk, requested, &count, NULL)) {
			dynlex_runtime_set_windows_error("Could not capture buffered Windows process output", GetLastError());
			return -1;
		}
		if (count == 0)
			break;
		dynlex_platform_process_lock(process);
		int append_result = dynlex_process_append_output(process, stream, chunk, count);
		dynlex_platform_process_unlock(process);
		if (append_result != 0)
			return -1;
		remaining -= count;
	}
	return 0;
}

static int capture_output_after_exit(DynlexProcess *process, DynlexWindowsProcess *platform) {
	stop_reader(platform, &platform->output_thread);
	stop_reader(platform, &platform->error_thread);
	int result = capture_buffered_pipe_after_exit(process, DYNLEX_PROCESS_STREAM_STDOUT, &platform->standard_output);
	if (result == 0)
		result = capture_buffered_pipe_after_exit(process, DYNLEX_PROCESS_STREAM_STDERR, &platform->standard_error);
	close_handle(&platform->standard_output);
	close_handle(&platform->standard_error);
	dynlex_platform_process_lock(process);
	dynlex_process_mark_stream_closed(process, DYNLEX_PROCESS_STREAM_STDOUT);
	dynlex_process_mark_stream_closed(process, DYNLEX_PROCESS_STREAM_STDERR);
	dynlex_platform_process_unlock(process);
	return result;
}

static int mark_process_finished(DynlexProcess *process, DynlexWindowsProcess *platform) {
	if (process->finished)
		return 0;
	DWORD exit_code = 0;
	if (!GetExitCodeProcess(platform->process, &exit_code)) {
		dynlex_runtime_set_windows_error("Could not read Windows process exit status", GetLastError());
		return -1;
	}
	dynlex_process_mark_finished(process, (int64_t)(uint64_t)exit_code, 0);
	return 0;
}

int dynlex_platform_process_pump(
	DynlexProcess *process, int64_t timeout_milliseconds, DynlexProcessStream requested_stream
) {
	DynlexWindowsProcess *platform = process->platform;
	ULONGLONG started = GetTickCount64();
	while (true) {
		if (report_reader_error(platform) != 0)
			return -1;
		EnterCriticalSection(&platform->output_lock);
		bool ready = requested_stream_ready(process, requested_stream);
		LeaveCriticalSection(&platform->output_lock);
		if (process->finished) {
			if (capture_output_after_exit(process, platform) != 0)
				return -1;
			return report_reader_error(platform);
		}
		if (ready)
			return 0;
		DWORD wait_time = INFINITE;
		if (timeout_milliseconds >= 0) {
			ULONGLONG elapsed = GetTickCount64() - started;
			if (elapsed >= (uint64_t)timeout_milliseconds)
				wait_time = 0;
			else {
				uint64_t remaining = (uint64_t)timeout_milliseconds - elapsed;
				wait_time = remaining > MAXDWORD ? MAXDWORD : (DWORD)remaining;
			}
		}
		HANDLE handles[2] = {platform->process, platform->activity_event};
		DWORD handle_count = requested_stream == 0 ? 1 : 2;
		DWORD wait_result = WaitForMultipleObjects(handle_count, handles, FALSE, wait_time);
		if (wait_result == WAIT_OBJECT_0) {
			if (mark_process_finished(process, platform) != 0)
				return -1;
			continue;
		}
		if (wait_result == WAIT_OBJECT_0 + 1)
			continue;
		if (wait_result == WAIT_FAILED) {
			dynlex_runtime_set_windows_error("Could not wait for Windows process", GetLastError());
			return -1;
		}
		if (wait_result == WAIT_TIMEOUT)
			return 0;
		dynlex_runtime_set_error("Windows process wait returned an unsupported status");
		return -1;
	}
}

int dynlex_platform_process_write(DynlexProcess *process, const char *data, size_t length, size_t *written) {
	DynlexWindowsProcess *platform = process->platform;
	*written = 0;
	if (length == 0)
		return 0;
	if (platform->standard_input == NULL) {
		dynlex_runtime_set_error("Process standard input is closed");
		return -1;
	}
	while (*written < length) {
		DWORD chunk = length - *written > UINT32_MAX ? UINT32_MAX : (DWORD)(length - *written);
		DWORD count = 0;
		if (!WriteFile(platform->standard_input, data + *written, chunk, &count, NULL)) {
			dynlex_runtime_set_windows_error("Could not write Windows process standard input", GetLastError());
			close_handle(&platform->standard_input);
			return -1;
		}
		if (count == 0) {
			dynlex_runtime_set_error("Windows process standard input accepted zero bytes");
			return -1;
		}
		*written += count;
	}
	return report_reader_error(platform);
}

int dynlex_platform_process_close_input(DynlexProcess *process) {
	DynlexWindowsProcess *platform = process->platform;
	close_handle(&platform->standard_input);
	return 0;
}

int dynlex_platform_process_terminate(DynlexProcess *process) {
	DynlexWindowsProcess *platform = process->platform;
	if (TerminateProcess(platform->process, 1))
		return 0;
	DWORD error_number = GetLastError();
	if (error_number == ERROR_ACCESS_DENIED) {
		DWORD status = WaitForSingleObject(platform->process, 0);
		if (status == WAIT_OBJECT_0)
			return mark_process_finished(process, platform);
	}
	dynlex_runtime_set_windows_error("Could not terminate Windows process", error_number);
	return -1;
}

int dynlex_platform_process_kill(DynlexProcess *process) { return dynlex_platform_process_terminate(process); }

int dynlex_platform_process_cleanup(DynlexProcess *process) {
	DynlexWindowsProcess *platform = process->platform;
	close_handle(&platform->standard_input);
	if (!process->finished) {
		if (TerminateProcess(platform->process, 1))
			process->termination_requested = true;
		else {
			DWORD error_number = GetLastError();
			DWORD status = WaitForSingleObject(platform->process, 0);
			if (status != WAIT_OBJECT_0) {
				dynlex_runtime_set_windows_error("Could not kill Windows process during cleanup", error_number);
				return -1;
			}
		}
	}
	cancel_reader(platform, &platform->output_thread, &platform->standard_output);
	cancel_reader(platform, &platform->error_thread, &platform->standard_error);
	DWORD wait_result = WaitForSingleObject(platform->process, INFINITE);
	if (wait_result != WAIT_OBJECT_0) {
		dynlex_runtime_set_windows_error("Could not wait for Windows process during cleanup", GetLastError());
		return -1;
	}
	if (mark_process_finished(process, platform) != 0)
		return -1;
	process->cleanup_ready = true;
	return report_reader_error(platform);
}

void dynlex_platform_process_destroy(DynlexProcess *process) {
	DynlexWindowsProcess *platform = process->platform;
	if (platform == NULL)
		return;
	if (!process->cleanup_ready)
		abort();
	close_handle(&platform->standard_input);
	cancel_reader(platform, &platform->output_thread, &platform->standard_output);
	cancel_reader(platform, &platform->error_thread, &platform->standard_error);
	close_handle(&platform->process);
	close_handle(&platform->activity_event);
	DeleteCriticalSection(&platform->output_lock);
	free(platform);
}

void dynlex_platform_process_lock(DynlexProcess *process) {
	DynlexWindowsProcess *platform = process->platform;
	EnterCriticalSection(&platform->output_lock);
}

void dynlex_platform_process_unlock(DynlexProcess *process) {
	DynlexWindowsProcess *platform = process->platform;
	LeaveCriticalSection(&platform->output_lock);
}
