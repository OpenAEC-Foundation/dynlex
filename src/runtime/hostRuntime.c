#include "hostRuntimeInternal.h"

#include "runtimeError.h"

#include <errno.h>
#include <stdbool.h>
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

int32_t dynlex_host_platform_is_windows(void) {
#ifdef _WIN32
	return 1;
#else
	return 0;
#endif
}

int dynlex_host_executable_path(char *output, size_t capacity, size_t *output_length) {
	dynlex_runtime_clear_error();
	char *path = NULL;
	size_t length = 0;
	if (dynlex_platform_executable_path(&path, &length) != 0)
		return -1;
	int result = write_host_output(path, length, output, capacity, output_length);
	free(path);
	return result;
}

int dynlex_host_executable_directory(char *output, size_t capacity, size_t *output_length) {
	dynlex_runtime_clear_error();
	char *path = NULL;
	size_t length = 0;
	if (dynlex_platform_executable_path(&path, &length) != 0)
		return -1;

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

int dynlex_host_read_standard_input_line(char **contents, size_t *length, int32_t *end_of_file) {
	dynlex_runtime_clear_error();
	if (contents == NULL || length == NULL || end_of_file == NULL) {
		dynlex_runtime_set_errno_error("Invalid standard input result arguments", EINVAL);
		return -1;
	}
	*contents = NULL;
	*length = 0;
	*end_of_file = 0;

	char *buffer = NULL;
	size_t capacity = 0;
	bool line_ended = false;
	for (;;) {
		errno = 0;
		int value = fgetc(stdin);
		if (value == EOF) {
			if (ferror(stdin)) {
				int error_number = errno == 0 ? EIO : errno;
				free(buffer);
				dynlex_runtime_set_errno_error("Could not read standard input", error_number);
				return -1;
			}
			*end_of_file = 1;
			break;
		}
		if (value == '\n') {
			line_ended = true;
			break;
		}
		if (*length == INT32_MAX) {
			free(buffer);
			dynlex_runtime_set_error("Standard input line exceeds the maximum string length");
			return -1;
		}
		if (*length == capacity) {
			size_t next_capacity = capacity == 0 ? 256 : capacity * 2;
			if (next_capacity > INT32_MAX)
				next_capacity = INT32_MAX;
			char *resized = realloc(buffer, next_capacity);
			if (resized == NULL) {
				free(buffer);
				dynlex_runtime_set_errno_error("Could not allocate standard input line", ENOMEM);
				return -1;
			}
			buffer = resized;
			capacity = next_capacity;
		}
		buffer[(*length)++] = (char)value;
	}
	if (line_ended && *length != 0 && buffer[*length - 1] == '\r')
		--*length;
	*contents = buffer;
	return 0;
}
