#ifndef DYNLEX_FILESYSTEM_RUNTIME_INTERNAL_H
#define DYNLEX_FILESYSTEM_RUNTIME_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

enum {
	DYNLEX_FILESYSTEM_ENTRY_REGULAR_FILE = 1,
	DYNLEX_FILESYSTEM_ENTRY_DIRECTORY = 2,
	DYNLEX_FILESYSTEM_ENTRY_SYMBOLIC_LINK = 3,
	DYNLEX_FILESYSTEM_ENTRY_OTHER = 4,
};

enum {
	DYNLEX_FILESYSTEM_OPEN_READ = 1,
	DYNLEX_FILESYSTEM_OPEN_WRITE = 2,
	DYNLEX_FILESYSTEM_OPEN_APPEND = 3,
};

bool dynlex_filesystem_utf8_is_valid(const char *data, size_t length);
char *dynlex_filesystem_copy_path(const char *path, size_t length);

FILE *dynlex_platform_filesystem_open_file(const char *path, size_t path_length, int32_t mode);
void dynlex_platform_filesystem_directory_destroy(void *directory);
int dynlex_platform_filesystem_create_temporary_directory(char **path, size_t *length);
int dynlex_platform_filesystem_remove_tree(const char *path, size_t path_length);

#endif
