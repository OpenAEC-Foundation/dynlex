#include "platformFeatureTest.h"

#include "filesystemRuntimeInternal.h"
#include "filesystemStatPosix.h"

#include "runtimeError.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

typedef struct {
	size_t references;
	DIR *stream;
	char *current_name;
	size_t current_name_length;
} DynlexPosixDirectory;

static int32_t entry_kind(mode_t mode) {
	if (S_ISREG(mode))
		return DYNLEX_FILESYSTEM_ENTRY_REGULAR_FILE;
	if (S_ISDIR(mode))
		return DYNLEX_FILESYSTEM_ENTRY_DIRECTORY;
	if (S_ISLNK(mode))
		return DYNLEX_FILESYSTEM_ENTRY_SYMBOLIC_LINK;
	return DYNLEX_FILESYSTEM_ENTRY_OTHER;
}

static int64_t modification_time(const struct stat *attributes) {
	return dynlex_stat_modification_seconds(attributes) * INT64_C(1000) +
		   dynlex_stat_modification_nanoseconds(attributes) / 1000000;
}

static bool missing_error(int error_number) { return error_number == ENOENT || error_number == ENOTDIR; }

static void trim_trailing_separators(char *path) {
	size_t length = strlen(path);
	while (length > 1 && path[length - 1] == '/')
		path[--length] = '\0';
}

FILE *dynlex_platform_filesystem_open_file(const char *path, size_t path_length, int32_t mode) {
	char *prepared = dynlex_filesystem_copy_path(path, path_length);
	if (prepared == NULL)
		return NULL;
	trim_trailing_separators(prepared);
	const char *mode_text = mode == DYNLEX_FILESYSTEM_OPEN_READ		? "rb"
							: mode == DYNLEX_FILESYSTEM_OPEN_WRITE	? "wb"
							: mode == DYNLEX_FILESYSTEM_OPEN_APPEND ? "ab"
																	: NULL;
	if (mode_text == NULL) {
		free(prepared);
		dynlex_runtime_set_error("Invalid filesystem open mode");
		return NULL;
	}
	FILE *file = fopen(prepared, mode_text);
	free(prepared);
	if (file == NULL)
		dynlex_runtime_set_errno_error("Could not open file", errno);
	return file;
}

int dynlex_filesystem_status(const char *path, size_t path_length, int32_t *kind, int64_t *modification_time_output) {
	dynlex_runtime_clear_error();
	if (kind == NULL || modification_time_output == NULL) {
		dynlex_runtime_set_error("Invalid filesystem status outputs");
		return -1;
	}
	char *prepared = dynlex_filesystem_copy_path(path, path_length);
	if (prepared == NULL)
		return -1;
	trim_trailing_separators(prepared);
	struct stat attributes;
	if (lstat(prepared, &attributes) != 0) {
		int error_number = errno;
		free(prepared);
		if (missing_error(error_number))
			return 0;
		dynlex_runtime_set_errno_error("Could not read filesystem status", error_number);
		return -1;
	}
	free(prepared);
	*kind = entry_kind(attributes.st_mode);
	*modification_time_output = modification_time(&attributes);
	return 1;
}

void *dynlex_filesystem_directory_open(const char *path, size_t path_length) {
	dynlex_runtime_clear_error();
	char *prepared = dynlex_filesystem_copy_path(path, path_length);
	if (prepared == NULL)
		return NULL;
	trim_trailing_separators(prepared);
	int descriptor = open(prepared, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
	int open_error = errno;
	free(prepared);
	if (descriptor < 0) {
		dynlex_runtime_set_errno_error("Could not open directory", open_error);
		return NULL;
	}
	DIR *stream = fdopendir(descriptor);
	if (stream == NULL) {
		int error_number = errno;
		close(descriptor);
		dynlex_runtime_set_errno_error("Could not open directory stream", error_number);
		return NULL;
	}
	DynlexPosixDirectory *directory = calloc(1, sizeof(*directory));
	if (directory == NULL) {
		closedir(stream);
		dynlex_runtime_set_errno_error("Could not allocate directory enumeration", ENOMEM);
		return NULL;
	}
	directory->stream = stream;
	return directory;
}

int dynlex_filesystem_directory_next(DynlexPosixDirectory *directory, int32_t *kind, size_t *name_length) {
	dynlex_runtime_clear_error();
	if (directory == NULL || kind == NULL || name_length == NULL) {
		dynlex_runtime_set_error("Invalid directory enumeration arguments");
		return -1;
	}
	for (;;) {
		errno = 0;
		struct dirent *entry = readdir(directory->stream);
		if (entry == NULL) {
			if (errno != 0) {
				dynlex_runtime_set_errno_error("Could not enumerate directory", errno);
				return -1;
			}
			return 0;
		}
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
			continue;
		size_t length = strlen(entry->d_name);
		if (!dynlex_filesystem_utf8_is_valid(entry->d_name, length)) {
			dynlex_runtime_set_error("Directory entry name is not valid UTF-8");
			return -1;
		}
		struct stat attributes;
		if (fstatat(dirfd(directory->stream), entry->d_name, &attributes, AT_SYMLINK_NOFOLLOW) != 0) {
			dynlex_runtime_set_errno_error("Could not inspect directory entry", errno);
			return -1;
		}
		char *resized = realloc(directory->current_name, length + 1);
		if (resized == NULL) {
			dynlex_runtime_set_errno_error("Could not store directory entry name", ENOMEM);
			return -1;
		}
		directory->current_name = resized;
		memcpy(directory->current_name, entry->d_name, length + 1);
		directory->current_name_length = length;
		*kind = entry_kind(attributes.st_mode);
		*name_length = length;
		return 1;
	}
}

int dynlex_filesystem_directory_copy_name(const DynlexPosixDirectory *directory, char *buffer, size_t capacity) {
	dynlex_runtime_clear_error();
	if (directory == NULL || directory->current_name == NULL || buffer == NULL || capacity <= directory->current_name_length) {
		dynlex_runtime_set_error("Directory entry name buffer is too small");
		return -1;
	}
	memcpy(buffer, directory->current_name, directory->current_name_length + 1);
	return 0;
}

void dynlex_platform_filesystem_directory_destroy(void *opaque_directory) {
	DynlexPosixDirectory *directory = opaque_directory;
	closedir(directory->stream);
	free(directory->current_name);
	free(directory);
}

static int ensure_directory(const char *path) {
	struct stat attributes;
	if (lstat(path, &attributes) == 0) {
		if (S_ISDIR(attributes.st_mode))
			return 0;
		dynlex_runtime_set_error("Filesystem path component is not a directory");
		return -1;
	}
	if (errno != ENOENT) {
		dynlex_runtime_set_errno_error("Could not inspect directory path", errno);
		return -1;
	}
	if (mkdir(path, 0777) == 0)
		return 0;
	if (errno == EEXIST && lstat(path, &attributes) == 0 && S_ISDIR(attributes.st_mode))
		return 0;
	dynlex_runtime_set_errno_error("Could not create directory", errno);
	return -1;
}

int dynlex_filesystem_create_directories(const char *path, size_t path_length) {
	dynlex_runtime_clear_error();
	char *prepared = dynlex_filesystem_copy_path(path, path_length);
	if (prepared == NULL)
		return -1;
	trim_trailing_separators(prepared);
	size_t prepared_length = strlen(prepared);
	for (size_t index = 1; index < prepared_length; ++index) {
		if (prepared[index] != '/')
			continue;
		if (prepared[index - 1] == '/')
			continue;
		prepared[index] = '\0';
		if (prepared[0] != '\0' && ensure_directory(prepared) != 0) {
			free(prepared);
			return -1;
		}
		prepared[index] = '/';
	}
	int result = ensure_directory(prepared);
	free(prepared);
	return result;
}

static int remove_directory_contents(DIR *directory) {
	for (;;) {
		errno = 0;
		struct dirent *entry = readdir(directory);
		if (entry == NULL) {
			if (errno == 0)
				return 0;
			dynlex_runtime_set_errno_error("Could not enumerate directory during cleanup", errno);
			return -1;
		}
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
			continue;
		struct stat attributes;
		int descriptor = dirfd(directory);
		if (fstatat(descriptor, entry->d_name, &attributes, AT_SYMLINK_NOFOLLOW) != 0) {
			dynlex_runtime_set_errno_error("Could not inspect filesystem entry during cleanup", errno);
			return -1;
		}
		if (S_ISDIR(attributes.st_mode)) {
			int child_descriptor = openat(descriptor, entry->d_name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
			if (child_descriptor < 0) {
				dynlex_runtime_set_errno_error("Could not open directory during cleanup", errno);
				return -1;
			}
			DIR *child = fdopendir(child_descriptor);
			if (child == NULL) {
				int error_number = errno;
				close(child_descriptor);
				dynlex_runtime_set_errno_error("Could not open directory stream during cleanup", error_number);
				return -1;
			}
			int child_result = remove_directory_contents(child);
			int close_result = closedir(child);
			if (child_result != 0)
				return -1;
			if (close_result != 0) {
				dynlex_runtime_set_errno_error("Could not close directory during cleanup", errno);
				return -1;
			}
			if (unlinkat(descriptor, entry->d_name, AT_REMOVEDIR) != 0) {
				dynlex_runtime_set_errno_error("Could not remove directory during cleanup", errno);
				return -1;
			}
		} else if (unlinkat(descriptor, entry->d_name, 0) != 0) {
			dynlex_runtime_set_errno_error("Could not remove filesystem entry during cleanup", errno);
			return -1;
		}
	}
}

int dynlex_platform_filesystem_remove_tree(const char *path, size_t path_length) {
	char *prepared = dynlex_filesystem_copy_path(path, path_length);
	if (prepared == NULL)
		return -1;
	trim_trailing_separators(prepared);
	struct stat attributes;
	if (lstat(prepared, &attributes) != 0) {
		int error_number = errno;
		free(prepared);
		if (missing_error(error_number))
			return 0;
		dynlex_runtime_set_errno_error("Could not inspect cleanup path", error_number);
		return -1;
	}
	if (!S_ISDIR(attributes.st_mode)) {
		int result = unlink(prepared);
		int error_number = errno;
		free(prepared);
		if (result != 0) {
			dynlex_runtime_set_errno_error("Could not remove filesystem entry", error_number);
			return -1;
		}
		return 0;
	}
	int descriptor = open(prepared, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
	if (descriptor < 0) {
		int error_number = errno;
		free(prepared);
		dynlex_runtime_set_errno_error("Could not open cleanup directory", error_number);
		return -1;
	}
	DIR *directory = fdopendir(descriptor);
	if (directory == NULL) {
		int error_number = errno;
		close(descriptor);
		free(prepared);
		dynlex_runtime_set_errno_error("Could not open cleanup directory stream", error_number);
		return -1;
	}
	int result = remove_directory_contents(directory);
	int close_result = closedir(directory);
	if (result == 0 && close_result != 0) {
		dynlex_runtime_set_errno_error("Could not close cleanup directory", errno);
		result = -1;
	}
	if (result == 0 && rmdir(prepared) != 0) {
		dynlex_runtime_set_errno_error("Could not remove cleanup directory", errno);
		result = -1;
	}
	free(prepared);
	return result;
}

int dynlex_filesystem_remove_tree(const char *path, size_t path_length) {
	dynlex_runtime_clear_error();
	return dynlex_platform_filesystem_remove_tree(path, path_length);
}

int dynlex_filesystem_rename(const char *source, size_t source_length, const char *destination, size_t destination_length) {
	dynlex_runtime_clear_error();
	char *prepared_source = dynlex_filesystem_copy_path(source, source_length);
	if (prepared_source == NULL)
		return -1;
	char *prepared_destination = dynlex_filesystem_copy_path(destination, destination_length);
	if (prepared_destination == NULL) {
		free(prepared_source);
		return -1;
	}
	int result = rename(prepared_source, prepared_destination);
	int error_number = errno;
	free(prepared_source);
	free(prepared_destination);
	if (result != 0) {
		dynlex_runtime_set_errno_error("Could not rename filesystem entry", error_number);
		return -1;
	}
	return 0;
}

int dynlex_platform_filesystem_create_temporary_directory(char **path, size_t *length) {
	const char *base = getenv("TMPDIR");
	if (base == NULL || base[0] == '\0')
		base = "/tmp";
	size_t base_length = strlen(base);
	char *absolute_base = NULL;
	if (base[0] == '/')
		absolute_base = dynlex_filesystem_copy_path(base, base_length);
	else {
		char *working_directory = getcwd(NULL, 0);
		if (working_directory == NULL) {
			dynlex_runtime_set_errno_error("Could not resolve host temporary directory", errno);
			return -1;
		}
		size_t working_length = strlen(working_directory);
		if (working_length > SIZE_MAX - base_length - 2) {
			free(working_directory);
			dynlex_runtime_set_error("Host temporary directory path is too large");
			return -1;
		}
		absolute_base = malloc(working_length + base_length + 2);
		if (absolute_base != NULL)
			snprintf(absolute_base, working_length + base_length + 2, "%s/%s", working_directory, base);
		free(working_directory);
		if (absolute_base == NULL) {
			dynlex_runtime_set_errno_error("Could not allocate host temporary directory path", ENOMEM);
			return -1;
		}
	}
	size_t absolute_length = strlen(absolute_base);
	static const char suffix[] = "/dynlex-XXXXXX";
	if (absolute_length > SIZE_MAX - sizeof(suffix)) {
		free(absolute_base);
		dynlex_runtime_set_error("Host temporary directory path is too large");
		return -1;
	}
	char *template = malloc(absolute_length + sizeof(suffix));
	if (template == NULL) {
		free(absolute_base);
		dynlex_runtime_set_errno_error("Could not allocate temporary directory path", ENOMEM);
		return -1;
	}
	memcpy(template, absolute_base, absolute_length);
	memcpy(template + absolute_length, suffix, sizeof(suffix));
	free(absolute_base);
	if (mkdtemp(template) == NULL) {
		int error_number = errno;
		free(template);
		dynlex_runtime_set_errno_error("Could not create temporary directory", error_number);
		return -1;
	}
	*path = template;
	*length = strlen(template);
	return 0;
}
