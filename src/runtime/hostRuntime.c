#include "hostRuntimeInternal.h"

#include "runtimeError.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int write_host_output(const char *value, size_t length, char *output, size_t capacity, size_t *output_length) {
	if (output_length == NULL || (output == NULL && capacity != 0)) {
		dynlex_runtime_set_errno_error("Invalid host output arguments", EINVAL);
		return -1;
	}
	*output_length = length;
	if (output == NULL)
		return 0;
	if (capacity < length) {
		dynlex_runtime_set_error("Host output buffer is too small");
		return -1;
	}
	if (length != 0)
		memcpy(output, value, length);
	return 0;
}

size_t dynlex_host_error_message(char *buffer, size_t capacity) { return dynlex_runtime_error_message(buffer, capacity); }

typedef int (*DynlexHostPathProvider)(char **path, size_t *length, int32_t *supported);

static int retrieve_host_path(
	char *output, size_t capacity, size_t *output_length, int32_t *supported, DynlexHostPathProvider provider,
	const char *invalid_support_message
) {
	if (supported == NULL) {
		dynlex_runtime_set_errno_error(invalid_support_message, EINVAL);
		return -1;
	}
	*supported = 1;
	char *path = NULL;
	size_t length = 0;
	if (provider(&path, &length, supported) != 0)
		return -1;
	if (*supported == 0)
		return write_host_output(NULL, 0, output, capacity, output_length);
	int result = write_host_output(path, length, output, capacity, output_length);
	free(path);
	return result;
}

int dynlex_host_platform_is_windows(int32_t *is_windows, int32_t *supported) {
	dynlex_runtime_clear_error();
	if (is_windows == NULL || supported == NULL) {
		dynlex_runtime_set_errno_error("Invalid host platform result arguments", EINVAL);
		return -1;
	}
#ifdef _WIN32
	*is_windows = 1;
#else
	*is_windows = 0;
#endif
	*supported = 1;
	return 0;
}

int dynlex_host_executable_path(char *output, size_t capacity, size_t *output_length, int32_t *supported) {
	dynlex_runtime_clear_error();
	return retrieve_host_path(
		output, capacity, output_length, supported, dynlex_platform_executable_path,
		"Invalid executable path support result argument"
	);
}

int dynlex_host_user_cache_directory(char *output, size_t capacity, size_t *output_length, int32_t *supported) {
	dynlex_runtime_clear_error();
	return retrieve_host_path(
		output, capacity, output_length, supported, dynlex_platform_user_cache_directory,
		"Invalid user cache directory support result argument"
	);
}

int dynlex_host_executable_directory(char *output, size_t capacity, size_t *output_length, int32_t *supported) {
	dynlex_runtime_clear_error();
	if (supported == NULL) {
		dynlex_runtime_set_errno_error("Invalid executable directory support result argument", EINVAL);
		return -1;
	}
	*supported = 1;
	char *path = NULL;
	size_t length = 0;
	if (dynlex_platform_executable_path(&path, &length, supported) != 0)
		return -1;
	if (*supported == 0)
		return write_host_output(NULL, 0, output, capacity, output_length);

	size_t directory_length = length;
	while (directory_length != 0 && path[directory_length - 1] != '/')
		--directory_length;
	if (directory_length == 0) {
		free(path);
		dynlex_runtime_set_error("The executable path has no directory");
		return -1;
	}
	if (directory_length > 1 && !(directory_length == 3 && path[1] == ':'))
		--directory_length;
	int result = write_host_output(path, directory_length, output, capacity, output_length);
	free(path);
	return result;
}

int dynlex_host_read_standard_input(char **contents, size_t *length, int32_t *end_of_file, int32_t *supported) {
	dynlex_runtime_clear_error();
	if (contents == NULL || length == NULL || end_of_file == NULL || supported == NULL) {
		dynlex_runtime_set_errno_error("Invalid standard input result arguments", EINVAL);
		return -1;
	}
	*contents = NULL;
	*length = 0;
	*end_of_file = 0;
	*supported = 1;
	if (dynlex_platform_prepare_standard_input() != 0)
		return -1;

	enum { STANDARD_INPUT_CHUNK_SIZE = 4096 };
	char *buffer = malloc(STANDARD_INPUT_CHUNK_SIZE);
	if (buffer == NULL) {
		dynlex_runtime_set_errno_error("Could not allocate standard input chunk", ENOMEM);
		return -1;
	}
	errno = 0;
	*length = fread(buffer, 1, STANDARD_INPUT_CHUNK_SIZE, stdin);
	if (ferror(stdin)) {
		int error_number = errno == 0 ? EIO : errno;
		free(buffer);
		*length = 0;
		dynlex_runtime_set_errno_error("Could not read standard input", error_number);
		return -1;
	}
	*end_of_file = feof(stdin) ? 1 : 0;
	if (*length == 0) {
		free(buffer);
		buffer = NULL;
	}
	*contents = buffer;
	return 0;
}

typedef int (*DynlexHostLookupProvider)(const char *, size_t, char **, size_t *, int32_t *);

static int host_lookup(
	const char *name, size_t name_length, char *output, size_t capacity, size_t *output_length, int32_t *found,
	int32_t *supported, DynlexHostLookupProvider provider
) {
	dynlex_runtime_clear_error();
	if (name == NULL || output_length == NULL || found == NULL || supported == NULL || (output == NULL && capacity != 0)) {
		dynlex_runtime_set_errno_error("Invalid host lookup arguments", EINVAL);
		return -1;
	}
	*supported = 1;
	char *value = NULL;
	size_t length = 0;
	if (provider(name, name_length, &value, &length, found) != 0)
		return -1;
	int result = write_host_output(value, length, output, capacity, output_length);
	free(value);
	return result;
}

int dynlex_host_environment_value(
	const char *name, size_t name_length, char *output, size_t capacity, size_t *output_length, int32_t *found,
	int32_t *supported
) {
	return host_lookup(name, name_length, output, capacity, output_length, found, supported, dynlex_platform_environment_value);
}

int dynlex_host_find_executable(
	const char *name, size_t name_length, char *output, size_t capacity, size_t *output_length, int32_t *found,
	int32_t *supported
) {
	return host_lookup(name, name_length, output, capacity, output_length, found, supported, dynlex_platform_find_executable);
}

int dynlex_host_platform_name(char *output, size_t capacity, size_t *output_length) {
	dynlex_runtime_clear_error();
	const char *name = dynlex_platform_name();
	return write_host_output(name, strlen(name), output, capacity, output_length);
}

int dynlex_host_is_administrator(int32_t *administrator, int32_t *supported) {
	dynlex_runtime_clear_error();
	if (administrator == NULL || supported == NULL) {
		dynlex_runtime_set_errno_error("Invalid administrator result argument", EINVAL);
		return -1;
	}
	*supported = 1;
	return dynlex_platform_is_administrator(administrator);
}

int dynlex_host_write_standard_error(const char *contents, size_t length, int32_t *supported) {
	dynlex_runtime_clear_error();
	if ((contents == NULL && length != 0) || supported == NULL) {
		dynlex_runtime_set_errno_error("Invalid standard error contents", EINVAL);
		return -1;
	}
	*supported = 1;
	if (length != 0 && fwrite(contents, 1, length, stderr) != length) {
		dynlex_runtime_set_errno_error("Could not write standard error", errno == 0 ? EIO : errno);
		return -1;
	}
	if (fflush(stderr) != 0) {
		dynlex_runtime_set_errno_error("Could not flush standard error", errno == 0 ? EIO : errno);
		return -1;
	}
	return 0;
}

void dynlex_host_exit(int32_t status) { exit(status); }
