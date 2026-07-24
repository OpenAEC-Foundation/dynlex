#ifndef DYNLEX_PATH_RUNTIME_INTERNAL_H
#define DYNLEX_PATH_RUNTIME_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
	DYNLEX_PATH_STYLE_POSIX = 1,
	DYNLEX_PATH_STYLE_WINDOWS = 2,
};

typedef enum {
	ROOT_POSIX_RELATIVE,
	ROOT_POSIX_SINGLE,
	ROOT_POSIX_DOUBLE,
	ROOT_WINDOWS_RELATIVE,
	ROOT_WINDOWS_ROOTED,
	ROOT_WINDOWS_DRIVE_RELATIVE,
	ROOT_WINDOWS_DRIVE_ABSOLUTE,
	ROOT_WINDOWS_UNC,
} RootKind;

typedef struct {
	char *data;
	size_t length;
	size_t capacity;
} Buffer;

typedef struct {
	char *text;
	size_t length;
	size_t root_length;
	RootKind root_kind;
	size_t component_count;
	size_t *component_offsets;
	size_t *component_lengths;
} ParsedPath;

bool dynlex_path_ascii_alpha(char value);
char dynlex_path_ascii_lower(char value);
int dynlex_path_buffer_append(Buffer *buffer, const char *data, size_t length);
int dynlex_path_buffer_append_byte(Buffer *buffer, char value);
void dynlex_path_free(ParsedPath *path);
int dynlex_path_validate_input(int32_t style, const char *text, size_t length);
int dynlex_path_parse(int32_t style, const char *text, size_t length, bool normalize_dot_segments, ParsedPath *parsed);
int dynlex_path_copy_bytes(const char *data, size_t length, char **result, size_t *result_length);
int dynlex_path_copy(const ParsedPath *path, char **result, size_t *result_length);
bool dynlex_path_fully_absolute(int32_t style, const ParsedPath *path);
int dynlex_path_prepare_owned_output(char **output, size_t *output_length);

#endif
