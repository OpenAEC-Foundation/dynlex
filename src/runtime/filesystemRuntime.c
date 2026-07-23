#include "filesystemRuntimeInternal.h"

#include "runtimeError.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct DynlexFilesystemStream {
	size_t references;
	FILE *file;
} DynlexFilesystemStream;

typedef struct DynlexFilesystemPathResult {
	size_t references;
	char *path;
	size_t length;
} DynlexFilesystemPathResult;

static bool continuation_byte(const unsigned char *data, size_t length, size_t index) {
	return index < length && (data[index] & 0xc0U) == 0x80U;
}

bool dynlex_filesystem_utf8_is_valid(const char *data, size_t length) {
	if (data == NULL || length == 0 || memchr(data, '\0', length) != NULL)
		return false;
	const unsigned char *bytes = (const unsigned char *)data;
	for (size_t index = 0; index < length;) {
		unsigned char first = bytes[index++];
		if (first <= 0x7fU)
			continue;
		if (first >= 0xc2U && first <= 0xdfU) {
			if (!continuation_byte(bytes, length, index))
				return false;
			index += 1;
			continue;
		}
		if (first >= 0xe0U && first <= 0xefU) {
			if (!continuation_byte(bytes, length, index) || !continuation_byte(bytes, length, index + 1))
				return false;
			unsigned char second = bytes[index];
			if ((first == 0xe0U && second < 0xa0U) || (first == 0xedU && second >= 0xa0U))
				return false;
			index += 2;
			continue;
		}
		if (first >= 0xf0U && first <= 0xf4U) {
			if (!continuation_byte(bytes, length, index) || !continuation_byte(bytes, length, index + 1) ||
				!continuation_byte(bytes, length, index + 2))
				return false;
			unsigned char second = bytes[index];
			if ((first == 0xf0U && second < 0x90U) || (first == 0xf4U && second >= 0x90U))
				return false;
			index += 3;
			continue;
		}
		return false;
	}
	return true;
}

char *dynlex_filesystem_copy_path(const char *path, size_t length) {
	if (!dynlex_filesystem_utf8_is_valid(path, length)) {
		dynlex_runtime_set_error("Filesystem paths must be nonempty UTF-8 text without zero bytes");
		return NULL;
	}
	if (length == SIZE_MAX) {
		dynlex_runtime_set_error("Filesystem path is too large");
		return NULL;
	}
	char *copy = malloc(length + 1);
	if (copy == NULL) {
		dynlex_runtime_set_errno_error("Could not allocate filesystem path", ENOMEM);
		return NULL;
	}
	memcpy(copy, path, length);
	copy[length] = '\0';
	return copy;
}

void dynlex_filesystem_clear_error(void) { dynlex_runtime_clear_error(); }

size_t dynlex_filesystem_error_message(char *buffer, size_t capacity) {
	if (dynlex_runtime_error_message(NULL, 0) == 0)
		dynlex_runtime_set_error("Filesystem operation failed");
	return dynlex_runtime_error_message(buffer, capacity);
}

void *dynlex_filesystem_file_open(const char *path, size_t path_length, int32_t mode) {
	dynlex_filesystem_clear_error();
	FILE *file = dynlex_platform_filesystem_open_file(path, path_length, mode);
	if (file == NULL)
		return NULL;
	DynlexFilesystemStream *stream = calloc(1, sizeof(*stream));
	if (stream == NULL) {
		int error_number = errno;
		fclose(file);
		dynlex_runtime_set_errno_error("Could not allocate filesystem stream", error_number == 0 ? ENOMEM : error_number);
		return NULL;
	}
	stream->file = file;
	return stream;
}

void *dynlex_filesystem_temporary_file_open(void) {
	dynlex_filesystem_clear_error();
#ifdef _WIN32
	FILE *file = NULL;
	errno_t open_result = tmpfile_s(&file);
	if (open_result != 0 || file == NULL) {
		dynlex_runtime_set_errno_error("Could not create temporary file", open_result == 0 ? EIO : open_result);
		return NULL;
	}
#else
	FILE *file = tmpfile();
	if (file == NULL) {
		dynlex_runtime_set_errno_error("Could not create temporary file", errno);
		return NULL;
	}
#endif
	DynlexFilesystemStream *stream = calloc(1, sizeof(*stream));
	if (stream == NULL) {
		fclose(file);
		dynlex_runtime_set_errno_error("Could not allocate filesystem stream", ENOMEM);
		return NULL;
	}
	stream->file = file;
	return stream;
}

void dynlex_filesystem_file_retain(DynlexFilesystemStream *stream) {
	if (stream != NULL) {
		if (stream->references == SIZE_MAX)
			abort();
		stream->references++;
	}
}

void dynlex_filesystem_file_release(DynlexFilesystemStream *stream) {
	if (stream == NULL)
		return;
	if (stream->references == 0)
		abort();
	if (--stream->references != 0)
		return;
	fclose(stream->file);
	free(stream);
}

int dynlex_filesystem_file_read(
	DynlexFilesystemStream *stream, char *buffer, size_t capacity, size_t *read_count, int32_t *end_of_file
) {
	dynlex_filesystem_clear_error();
	if (stream == NULL || (buffer == NULL && capacity > 0) || read_count == NULL || end_of_file == NULL) {
		dynlex_runtime_set_error("Invalid filesystem read arguments");
		return -1;
	}
	*read_count = fread(buffer, 1, capacity, stream->file);
	if (*read_count < capacity && ferror(stream->file)) {
		dynlex_runtime_set_errno_error("Could not read file", errno == 0 ? EIO : errno);
		return -1;
	}
	*end_of_file = feof(stream->file) ? 1 : 0;
	return 0;
}

int dynlex_filesystem_file_write(DynlexFilesystemStream *stream, const char *buffer, size_t count, size_t *written_count) {
	dynlex_filesystem_clear_error();
	if (stream == NULL || (buffer == NULL && count > 0) || written_count == NULL) {
		dynlex_runtime_set_error("Invalid filesystem write arguments");
		return -1;
	}
	*written_count = fwrite(buffer, 1, count, stream->file);
	if (*written_count != count) {
		dynlex_runtime_set_errno_error("Could not write file", errno == 0 ? EIO : errno);
		return -1;
	}
	return 0;
}

int dynlex_filesystem_file_finish(DynlexFilesystemStream *stream) {
	dynlex_filesystem_clear_error();
	if (stream == NULL) {
		dynlex_runtime_set_error("Invalid filesystem stream");
		return -1;
	}
	if (fflush(stream->file) != 0) {
		dynlex_runtime_set_errno_error("Could not flush file", errno);
		return -1;
	}
	return 0;
}

int dynlex_filesystem_file_rewind(DynlexFilesystemStream *stream) {
	dynlex_filesystem_clear_error();
	if (stream == NULL) {
		dynlex_runtime_set_error("Invalid filesystem stream");
		return -1;
	}
	rewind(stream->file);
	if (ferror(stream->file)) {
		dynlex_runtime_set_errno_error("Could not rewind file", errno == 0 ? EIO : errno);
		return -1;
	}
	return 0;
}

void dynlex_filesystem_directory_retain(void *directory) {
	if (directory == NULL)
		return;
	size_t *references = directory;
	if (*references == SIZE_MAX)
		abort();
	(*references)++;
}

void dynlex_filesystem_directory_release(void *directory) {
	if (directory == NULL)
		return;
	size_t *references = directory;
	if (*references == 0)
		abort();
	if (--(*references) == 0)
		dynlex_platform_filesystem_directory_destroy(directory);
}

void *dynlex_filesystem_temporary_directory_create(void) {
	dynlex_filesystem_clear_error();
	char *path = NULL;
	size_t length = 0;
	if (dynlex_platform_filesystem_create_temporary_directory(&path, &length) != 0)
		return NULL;
	DynlexFilesystemPathResult *result = calloc(1, sizeof(*result));
	if (result == NULL) {
		dynlex_platform_filesystem_remove_tree(path, length);
		free(path);
		dynlex_runtime_set_errno_error("Could not allocate temporary directory result", ENOMEM);
		return NULL;
	}
	result->path = path;
	result->length = length;
	return result;
}

size_t dynlex_filesystem_temporary_directory_path_length(const DynlexFilesystemPathResult *result) {
	return result == NULL ? 0 : result->length;
}

int dynlex_filesystem_temporary_directory_copy_path(const DynlexFilesystemPathResult *result, char *buffer, size_t capacity) {
	dynlex_filesystem_clear_error();
	if (result == NULL || buffer == NULL || capacity <= result->length) {
		dynlex_runtime_set_error("Temporary directory path buffer is too small");
		return -1;
	}
	memcpy(buffer, result->path, result->length);
	buffer[result->length] = '\0';
	return 0;
}

void dynlex_filesystem_temporary_directory_release(DynlexFilesystemPathResult *result) {
	if (result == NULL)
		return;
	if (result->references == 0)
		abort();
	if (--result->references != 0)
		return;
	free(result->path);
	free(result);
}

void dynlex_filesystem_temporary_directory_retain(DynlexFilesystemPathResult *result) {
	if (result != NULL) {
		if (result->references == SIZE_MAX)
			abort();
		result->references++;
	}
}
