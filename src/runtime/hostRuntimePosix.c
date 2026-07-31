#include "platformFeatureTest.h"

#include "hostRuntimeInternal.h"

#include "runtimeError.h"
#include "runtimeText.h"

#include <errno.h>
#include <pwd.h>
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

static int copy_cache_path(
	const char *base, size_t base_length, const char *suffix, size_t suffix_length, char **output, size_t *output_length
) {
	if (base_length == 0 || base[0] != '/') {
		dynlex_runtime_set_error("The user cache directory base path is not absolute");
		return -1;
	}
	if (!dynlex_runtime_is_valid_utf8(base, base_length)) {
		dynlex_runtime_set_error("The user cache directory base path is not valid UTF-8");
		return -1;
	}
	bool needs_separator = suffix_length != 0 && base[base_length - 1] != '/';
	size_t separator_length = needs_separator ? 1 : 0;
	if (base_length > (size_t)INT32_MAX - separator_length - suffix_length) {
		dynlex_runtime_set_error("The user cache directory path exceeds the maximum string length");
		return -1;
	}
	size_t length = base_length + separator_length + suffix_length;
	char *path = malloc(length);
	if (path == NULL) {
		dynlex_runtime_set_errno_error("Could not allocate user cache directory path", ENOMEM);
		return -1;
	}
	memcpy(path, base, base_length);
	size_t position = base_length;
	if (needs_separator)
		path[position++] = '/';
	if (suffix_length != 0)
		memcpy(path + position, suffix, suffix_length);
	*output = path;
	*output_length = length;
	return 0;
}

static int cache_directory_from_home(const char *home, char **path, size_t *length) {
#if defined(__APPLE__)
	static const char suffix[] = "Library/Caches";
#else
	static const char suffix[] = ".cache";
#endif
	return copy_cache_path(home, strlen(home), suffix, sizeof(suffix) - 1, path, length);
}

static int user_home_directory(char **path, size_t *length) {
	long configured_capacity = sysconf(_SC_GETPW_R_SIZE_MAX);
	size_t capacity = configured_capacity > 0 ? (size_t)configured_capacity : 1024;
	if (capacity > (size_t)INT32_MAX)
		capacity = (size_t)INT32_MAX;

	for (;;) {
		char *buffer = malloc(capacity);
		if (buffer == NULL) {
			dynlex_runtime_set_errno_error("Could not allocate user account lookup buffer", ENOMEM);
			return -1;
		}
		struct passwd record;
		struct passwd *result = NULL;
		int status = getpwuid_r(geteuid(), &record, buffer, capacity, &result);
		if (status == 0 && result != NULL) {
			int outcome = cache_directory_from_home(record.pw_dir, path, length);
			free(buffer);
			return outcome;
		}
		free(buffer);
		if (status != ERANGE) {
			dynlex_runtime_set_errno_error(
				"Could not retrieve the current user's home directory", status == 0 ? ENOENT : status
			);
			return -1;
		}
		if (capacity > (size_t)INT32_MAX / 2) {
			dynlex_runtime_set_error("The user account lookup exceeds the maximum buffer length");
			return -1;
		}
		capacity *= 2;
	}
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
#endif

int dynlex_platform_executable_path(char **path, size_t *length, int32_t *supported) {
	if (path == NULL || length == NULL || supported == NULL) {
		dynlex_runtime_set_errno_error("Invalid executable path result arguments", EINVAL);
		return -1;
	}
	*path = NULL;
	*length = 0;
	*supported = 0;
#if defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__)
	*supported = 1;
	return read_native_executable_path(path, length);
#else
	dynlex_runtime_set_error("Executable path retrieval is not supported on this POSIX platform");
	return 0;
#endif
}

int dynlex_platform_user_cache_directory(char **path, size_t *length, int32_t *supported) {
	if (path == NULL || length == NULL || supported == NULL) {
		dynlex_runtime_set_errno_error("Invalid user cache directory result arguments", EINVAL);
		return -1;
	}
	*path = NULL;
	*length = 0;
	*supported = 1;

	const char *xdg_cache_home = getenv("XDG_CACHE_HOME");
	if (xdg_cache_home != NULL && xdg_cache_home[0] == '/')
		return copy_cache_path(xdg_cache_home, strlen(xdg_cache_home), "", 0, path, length);

	const char *home = getenv("HOME");
	if (home != NULL && home[0] == '/')
		return cache_directory_from_home(home, path, length);

	return user_home_directory(path, length);
}

int dynlex_platform_prepare_standard_input(void) { return 0; }
