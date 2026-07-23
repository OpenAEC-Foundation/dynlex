#define _POSIX_C_SOURCE 200809L

#include "hostRuntimeInternal.h"

#include "runtimeError.h"
#include "runtimeText.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(__FreeBSD__)
#include <sys/sysctl.h>
#include <sys/types.h>
#endif

static int validate_executable_path(char *path, size_t length, char **output, size_t *output_length) {
	if (length == 0 || path[0] != '/') {
		free(path);
		dynlex_runtime_set_error("The operating system returned a non-absolute executable path");
		return -1;
	}
	if (!dynlex_runtime_is_valid_utf8(path, length)) {
		free(path);
		dynlex_runtime_set_error("The executable path is not valid UTF-8");
		return -1;
	}
	*output = path;
	*output_length = length;
	return 0;
}

#if defined(__linux__)
static int read_native_executable_path(char **output, size_t *output_length) {
	size_t capacity = 256;
	for (;;) {
		char *path = malloc(capacity);
		if (path == NULL) {
			dynlex_runtime_set_errno_error("Could not allocate executable path", ENOMEM);
			return -1;
		}
		ssize_t length = readlink("/proc/self/exe", path, capacity);
		if (length < 0) {
			int error_number = errno;
			free(path);
			dynlex_runtime_set_errno_error("Could not retrieve executable path", error_number);
			return -1;
		}
		if ((size_t)length < capacity)
			return validate_executable_path(path, (size_t)length, output, output_length);
		free(path);
		if (capacity > (size_t)INT32_MAX / 2) {
			dynlex_runtime_set_error("Executable path exceeds the maximum string length");
			return -1;
		}
		capacity *= 2;
	}
}
#elif defined(__APPLE__)
static int read_native_executable_path(char **output, size_t *output_length) {
	uint32_t capacity = 0;
	(void)_NSGetExecutablePath(NULL, &capacity);
	if (capacity == 0 || capacity > INT32_MAX) {
		dynlex_runtime_set_error("Executable path exceeds the maximum string length");
		return -1;
	}
	char *unresolved = malloc(capacity);
	if (unresolved == NULL) {
		dynlex_runtime_set_errno_error("Could not allocate executable path", ENOMEM);
		return -1;
	}
	if (_NSGetExecutablePath(unresolved, &capacity) != 0) {
		free(unresolved);
		dynlex_runtime_set_error("Could not retrieve executable path");
		return -1;
	}
	if (unresolved[0] != '/') {
		free(unresolved);
		dynlex_runtime_set_error("The operating system returned a non-absolute executable path");
		return -1;
	}
	char *resolved = realpath(unresolved, NULL);
	int error_number = errno;
	free(unresolved);
	if (resolved == NULL) {
		dynlex_runtime_set_errno_error("Could not resolve executable path", error_number);
		return -1;
	}
	return validate_executable_path(resolved, strlen(resolved), output, output_length);
}
#elif defined(__FreeBSD__)
static int read_native_executable_path(char **output, size_t *output_length) {
	int name[] = {CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1};
	size_t capacity = 0;
	if (sysctl(name, 4, NULL, &capacity, NULL, 0) != 0) {
		dynlex_runtime_set_errno_error("Could not measure executable path", errno);
		return -1;
	}
	if (capacity == 0 || capacity > INT32_MAX) {
		dynlex_runtime_set_error("Executable path exceeds the maximum string length");
		return -1;
	}
	char *path = malloc(capacity);
	if (path == NULL) {
		dynlex_runtime_set_errno_error("Could not allocate executable path", ENOMEM);
		return -1;
	}
	if (sysctl(name, 4, path, &capacity, NULL, 0) != 0) {
		int error_number = errno;
		free(path);
		dynlex_runtime_set_errno_error("Could not retrieve executable path", error_number);
		return -1;
	}
	size_t length = capacity != 0 && path[capacity - 1] == '\0' ? capacity - 1 : capacity;
	return validate_executable_path(path, length, output, output_length);
}
#else
static int read_native_executable_path(char **output, size_t *output_length) {
	(void)output;
	(void)output_length;
	dynlex_runtime_set_error("Executable path retrieval is not supported on this POSIX platform");
	return -1;
}
#endif

int dynlex_platform_executable_path(char **path, size_t *length) {
	if (path == NULL || length == NULL) {
		dynlex_runtime_set_errno_error("Invalid executable path result arguments", EINVAL);
		return -1;
	}
	*path = NULL;
	*length = 0;
	return read_native_executable_path(path, length);
}
