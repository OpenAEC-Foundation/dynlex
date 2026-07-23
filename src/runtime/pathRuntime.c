#include "runtimeError.h"
#include "runtimeText.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum {
	DYNLEX_PATH_STYLE_POSIX = 1,
	DYNLEX_PATH_STYLE_WINDOWS = 2,
};

enum {
	DYNLEX_PATH_NORMALIZE = 1,
	DYNLEX_PATH_PARENT = 2,
	DYNLEX_PATH_FILENAME = 3,
	DYNLEX_PATH_STEM = 4,
	DYNLEX_PATH_SUFFIX = 5,
};

enum {
	DYNLEX_PATH_JOIN = 1,
	DYNLEX_PATH_RESOLVE = 2,
	DYNLEX_PATH_RELATIVE = 3,
};

enum {
	DYNLEX_FILE_URI_ENCODE = 1,
	DYNLEX_FILE_URI_DECODE = 2,
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
	const char *data;
	size_t length;
} Component;

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
} NormalPath;

static bool ascii_alpha(char value) { return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z'); }

static char ascii_lower(char value) { return value >= 'A' && value <= 'Z' ? (char)(value + ('a' - 'A')) : value; }

static bool windows_separator(char value) { return value == '/' || value == '\\'; }

static bool path_separator(int32_t style, char value) {
	return style == DYNLEX_PATH_STYLE_WINDOWS ? windows_separator(value) : value == '/';
}

static int buffer_reserve(Buffer *buffer, size_t additional) {
	if (additional > SIZE_MAX - buffer->length) {
		dynlex_runtime_set_error("Path result is too large");
		return -1;
	}
	size_t required = buffer->length + additional;
	if (required <= buffer->capacity)
		return 0;
	size_t capacity = buffer->capacity == 0 ? 64 : buffer->capacity;
	while (capacity < required) {
		if (capacity > SIZE_MAX / 2) {
			capacity = required;
			break;
		}
		capacity *= 2;
	}
	char *resized = realloc(buffer->data, capacity);
	if (resized == NULL) {
		dynlex_runtime_set_errno_error("Could not allocate path result", ENOMEM);
		return -1;
	}
	buffer->data = resized;
	buffer->capacity = capacity;
	return 0;
}

static int buffer_append(Buffer *buffer, const char *data, size_t length) {
	if (buffer_reserve(buffer, length) != 0)
		return -1;
	if (length != 0)
		memcpy(buffer->data + buffer->length, data, length);
	buffer->length += length;
	return 0;
}

static int buffer_append_byte(Buffer *buffer, char value) { return buffer_append(buffer, &value, 1); }

static void free_normal_path(NormalPath *path) {
	free(path->text);
	free(path->component_offsets);
	free(path->component_lengths);
	memset(path, 0, sizeof(*path));
}

static int validate_path_input(int32_t style, const char *text, size_t length) {
	if (style != DYNLEX_PATH_STYLE_POSIX && style != DYNLEX_PATH_STYLE_WINDOWS) {
		dynlex_runtime_set_error("Unknown path style");
		return -1;
	}
	if ((text == NULL && length != 0) || length > INT32_MAX) {
		dynlex_runtime_set_error("Invalid path text");
		return -1;
	}
	if (length != 0 && memchr(text, '\0', length) != NULL) {
		dynlex_runtime_set_error("Path text contains a null byte");
		return -1;
	}
	return 0;
}

static bool component_equals(const Component *component, const char *value) {
	size_t length = strlen(value);
	return component->length == length && memcmp(component->data, value, length) == 0;
}

static int allocate_components(size_t input_length, Component **components) {
	if (input_length == SIZE_MAX || input_length + 1 > SIZE_MAX / sizeof(**components)) {
		dynlex_runtime_set_error("Path has too many components");
		return -1;
	}
	*components = malloc((input_length + 1) * sizeof(**components));
	if (*components == NULL) {
		dynlex_runtime_set_errno_error("Could not allocate path components", ENOMEM);
		return -1;
	}
	return 0;
}

static int append_root(int32_t style, const char *text, size_t length, size_t *cursor, RootKind *root_kind, Buffer *output) {
	*cursor = 0;
	if (style == DYNLEX_PATH_STYLE_POSIX) {
		size_t slash_count = 0;
		while (slash_count < length && text[slash_count] == '/')
			++slash_count;
		if (slash_count == 0) {
			*root_kind = ROOT_POSIX_RELATIVE;
			return 0;
		}
		*cursor = slash_count;
		*root_kind = slash_count == 2 ? ROOT_POSIX_DOUBLE : ROOT_POSIX_SINGLE;
		return buffer_append(output, slash_count == 2 ? "//" : "/", slash_count == 2 ? 2 : 1);
	}

	*root_kind = ROOT_WINDOWS_RELATIVE;
	if (length >= 4 && windows_separator(text[0]) && windows_separator(text[1]) && (text[2] == '?' || text[2] == '.') &&
		windows_separator(text[3])) {
		dynlex_runtime_set_error("Windows device namespace paths are not supported");
		return -1;
	}
	if (length >= 2 && ascii_alpha(text[0]) && text[1] == ':') {
		if (buffer_append(output, text, 2) != 0)
			return -1;
		*cursor = 2;
		if (*cursor < length && windows_separator(text[*cursor])) {
			*root_kind = ROOT_WINDOWS_DRIVE_ABSOLUTE;
			if (buffer_append_byte(output, '/') != 0)
				return -1;
			while (*cursor < length && windows_separator(text[*cursor]))
				++*cursor;
		} else {
			*root_kind = ROOT_WINDOWS_DRIVE_RELATIVE;
		}
		return 0;
	}
	if (length != 0 && windows_separator(text[0])) {
		bool unc = length >= 2 && windows_separator(text[1]) && (length == 2 || !windows_separator(text[2]));
		if (!unc) {
			*root_kind = ROOT_WINDOWS_ROOTED;
			while (*cursor < length && windows_separator(text[*cursor]))
				++*cursor;
			return buffer_append_byte(output, '/');
		}

		size_t server_start = 2;
		size_t server_end = server_start;
		while (server_end < length && !windows_separator(text[server_end]))
			++server_end;
		size_t share_start = server_end;
		while (share_start < length && windows_separator(text[share_start]))
			++share_start;
		size_t share_end = share_start;
		while (share_end < length && !windows_separator(text[share_end]))
			++share_end;
		if (server_end == server_start || share_end == share_start ||
			(server_end - server_start == 1 && (text[server_start] == '.' || text[server_start] == '?')) ||
			(share_end - share_start == 1 && text[share_start] == '.') ||
			(share_end - share_start == 2 && text[share_start] == '.' && text[share_start + 1] == '.')) {
			dynlex_runtime_set_error("A Windows UNC path requires a server and share");
			return -1;
		}
		if (buffer_append(output, "//", 2) != 0 || buffer_append(output, text + server_start, server_end - server_start) != 0 ||
			buffer_append_byte(output, '/') != 0 || buffer_append(output, text + share_start, share_end - share_start) != 0)
			return -1;
		*root_kind = ROOT_WINDOWS_UNC;
		*cursor = share_end;
		while (*cursor < length && windows_separator(text[*cursor]))
			++*cursor;
	}
	return 0;
}

static bool root_is_directory(RootKind kind) {
	return kind == ROOT_POSIX_SINGLE || kind == ROOT_POSIX_DOUBLE || kind == ROOT_WINDOWS_ROOTED ||
		   kind == ROOT_WINDOWS_DRIVE_ABSOLUTE || kind == ROOT_WINDOWS_UNC;
}

static int normalize_path(int32_t style, const char *text, size_t length, NormalPath *normalized) {
	memset(normalized, 0, sizeof(*normalized));
	if (validate_path_input(style, text, length) != 0)
		return -1;

	Buffer output = {0};
	Component *components = NULL;
	if (allocate_components(length, &components) != 0)
		return -1;
	size_t cursor;
	if (append_root(style, text, length, &cursor, &normalized->root_kind, &output) != 0) {
		free(components);
		free(output.data);
		return -1;
	}
	normalized->root_length = output.length;

	size_t component_count = 0;
	while (cursor < length) {
		while (cursor < length && path_separator(style, text[cursor]))
			++cursor;
		size_t start = cursor;
		while (cursor < length && !path_separator(style, text[cursor]))
			++cursor;
		Component component = {text + start, cursor - start};
		if (component.length == 0 || component_equals(&component, "."))
			continue;
		if (component_equals(&component, "..")) {
			if (component_count != 0 && !component_equals(&components[component_count - 1], "..")) {
				--component_count;
			} else if (!root_is_directory(normalized->root_kind)) {
				components[component_count++] = component;
			}
			continue;
		}
		components[component_count++] = component;
	}

	normalized->component_offsets = malloc((component_count == 0 ? 1 : component_count) * sizeof(size_t));
	normalized->component_lengths = malloc((component_count == 0 ? 1 : component_count) * sizeof(size_t));
	if (normalized->component_offsets == NULL || normalized->component_lengths == NULL) {
		free(components);
		free_normal_path(normalized);
		free(output.data);
		dynlex_runtime_set_errno_error("Could not allocate normalized path components", ENOMEM);
		return -1;
	}
	for (size_t index = 0; index < component_count; ++index) {
		bool drive_first = normalized->root_kind == ROOT_WINDOWS_DRIVE_RELATIVE && index == 0;
		if (output.length != 0 && output.data[output.length - 1] != '/' && !drive_first) {
			if (buffer_append_byte(&output, '/') != 0)
				goto failure;
		}
		normalized->component_offsets[index] = output.length;
		normalized->component_lengths[index] = components[index].length;
		if (buffer_append(&output, components[index].data, components[index].length) != 0)
			goto failure;
	}
	free(components);
	if (output.length == 0 && buffer_append_byte(&output, '.') != 0)
		goto failure_without_components;
	normalized->text = output.data;
	normalized->length = output.length;
	normalized->component_count = component_count;
	return 0;

failure:
	free(components);
failure_without_components:
	free(output.data);
	free_normal_path(normalized);
	return -1;
}

static int copy_bytes(const char *data, size_t length, char **result, size_t *result_length) {
	char *copy = NULL;
	if (length != 0) {
		copy = malloc(length);
		if (copy == NULL) {
			dynlex_runtime_set_errno_error("Could not allocate path result", ENOMEM);
			return -1;
		}
		memcpy(copy, data, length);
	}
	*result = copy;
	*result_length = length;
	return 0;
}

static int copy_normal_path(const NormalPath *path, char **result, size_t *result_length) {
	return copy_bytes(path->text, path->length, result, result_length);
}

static bool bytes_equal_ascii_case_insensitive(const char *left, const char *right, size_t length) {
	for (size_t index = 0; index < length; ++index) {
		if (ascii_lower(left[index]) != ascii_lower(right[index]))
			return false;
	}
	return true;
}

static bool roots_equal(int32_t style, const NormalPath *left, const NormalPath *right) {
	if (left->root_kind != right->root_kind || left->root_length != right->root_length)
		return false;
	if (style == DYNLEX_PATH_STYLE_WINDOWS)
		return bytes_equal_ascii_case_insensitive(left->text, right->text, left->root_length);
	return memcmp(left->text, right->text, left->root_length) == 0;
}

static bool fully_absolute(int32_t style, const NormalPath *path) {
	if (style == DYNLEX_PATH_STYLE_POSIX)
		return path->root_kind == ROOT_POSIX_SINGLE || path->root_kind == ROOT_POSIX_DOUBLE;
	return path->root_kind == ROOT_WINDOWS_DRIVE_ABSOLUTE || path->root_kind == ROOT_WINDOWS_UNC;
}

static bool absolute_path(int32_t style, const NormalPath *path) {
	return fully_absolute(style, path) || (style == DYNLEX_PATH_STYLE_WINDOWS && path->root_kind == ROOT_WINDOWS_ROOTED);
}

static int concatenate_and_normalize(
	int32_t style, const char *left, size_t left_length, bool separator, const char *right, size_t right_length, char **result,
	size_t *result_length
) {
	Buffer combined = {0};
	if (buffer_append(&combined, left, left_length) != 0 || (separator && buffer_append_byte(&combined, '/') != 0) ||
		buffer_append(&combined, right, right_length) != 0) {
		free(combined.data);
		return -1;
	}
	NormalPath normalized;
	int outcome = normalize_path(style, combined.data, combined.length, &normalized);
	free(combined.data);
	if (outcome != 0)
		return -1;
	outcome = copy_normal_path(&normalized, result, result_length);
	free_normal_path(&normalized);
	return outcome;
}

static int
join_normalized(int32_t style, const NormalPath *left, const NormalPath *right, char **result, size_t *result_length) {
	if (fully_absolute(style, right))
		return copy_normal_path(right, result, result_length);
	if (style == DYNLEX_PATH_STYLE_WINDOWS && right->root_kind == ROOT_WINDOWS_ROOTED) {
		if (left->root_length == 0 || left->root_kind == ROOT_WINDOWS_RELATIVE)
			return copy_normal_path(right, result, result_length);
		return concatenate_and_normalize(
			style, left->text, left->root_length, false, right->text, right->length, result, result_length
		);
	}
	if (style == DYNLEX_PATH_STYLE_WINDOWS && right->root_kind == ROOT_WINDOWS_DRIVE_RELATIVE) {
		bool left_has_same_drive =
			(left->root_kind == ROOT_WINDOWS_DRIVE_RELATIVE || left->root_kind == ROOT_WINDOWS_DRIVE_ABSOLUTE) &&
			bytes_equal_ascii_case_insensitive(left->text, right->text, 2);
		if (!left_has_same_drive)
			return copy_normal_path(right, result, result_length);
		const char *right_relative = right->text + 2;
		size_t right_relative_length = right->length - 2;
		return concatenate_and_normalize(
			style, left->text, left->length, right_relative_length != 0, right_relative, right_relative_length, result,
			result_length
		);
	}
	bool left_is_dot = left->root_length == 0 && left->component_count == 0 && left->length == 1 && left->text[0] == '.';
	bool right_is_dot = right->root_length == 0 && right->component_count == 0 && right->length == 1 && right->text[0] == '.';
	if (left_is_dot)
		return copy_normal_path(right, result, result_length);
	if (right_is_dot)
		return copy_normal_path(left, result, result_length);
	return concatenate_and_normalize(style, left->text, left->length, true, right->text, right->length, result, result_length);
}

static int parent_path(const NormalPath *path, char **result, size_t *result_length) {
	if (path->component_count == 0)
		return copy_normal_path(path, result, result_length);
	size_t parent_length = path->component_offsets[path->component_count - 1];
	if (parent_length > path->root_length && path->text[parent_length - 1] == '/')
		--parent_length;
	if (parent_length == 0)
		return copy_bytes(".", 1, result, result_length);
	return copy_bytes(path->text, parent_length, result, result_length);
}

static void filename_span(const NormalPath *path, const char **filename, size_t *filename_length) {
	if (path->component_count == 0) {
		*filename = NULL;
		*filename_length = 0;
		return;
	}
	size_t index = path->component_count - 1;
	*filename = path->text + path->component_offsets[index];
	*filename_length = path->component_lengths[index];
}

static int unary_result(int32_t operation, const NormalPath *path, char **result, size_t *result_length) {
	if (operation == DYNLEX_PATH_NORMALIZE)
		return copy_normal_path(path, result, result_length);
	if (operation == DYNLEX_PATH_PARENT)
		return parent_path(path, result, result_length);

	const char *filename;
	size_t filename_length;
	filename_span(path, &filename, &filename_length);
	if (operation == DYNLEX_PATH_FILENAME)
		return copy_bytes(filename, filename_length, result, result_length);
	size_t suffix_offset = filename_length;
	bool dot_directory =
		(filename_length == 1 && filename[0] == '.') || (filename_length == 2 && filename[0] == '.' && filename[1] == '.');
	if (!dot_directory) {
		for (size_t index = filename_length; index > 1; --index) {
			if (filename[index - 1] == '.') {
				suffix_offset = index - 1;
				break;
			}
		}
	}
	if (operation == DYNLEX_PATH_STEM)
		return copy_bytes(filename, suffix_offset, result, result_length);
	if (operation == DYNLEX_PATH_SUFFIX)
		return copy_bytes(
			filename == NULL ? NULL : filename + suffix_offset, filename_length - suffix_offset, result, result_length
		);
	dynlex_runtime_set_error("Unknown unary path operation");
	return -1;
}

static int
resolve_normalized(int32_t style, const NormalPath *path, const NormalPath *base, char **result, size_t *result_length) {
	if (!fully_absolute(style, base)) {
		dynlex_runtime_set_error("Path resolution requires an absolute base path");
		return -1;
	}
	if (fully_absolute(style, path))
		return copy_normal_path(path, result, result_length);
	if (style == DYNLEX_PATH_STYLE_WINDOWS && path->root_kind == ROOT_WINDOWS_ROOTED) {
		return concatenate_and_normalize(
			style, base->text, base->root_length, false, path->text, path->length, result, result_length
		);
	}
	if (style == DYNLEX_PATH_STYLE_WINDOWS && path->root_kind == ROOT_WINDOWS_DRIVE_RELATIVE) {
		if (base->root_kind != ROOT_WINDOWS_DRIVE_ABSOLUTE || !bytes_equal_ascii_case_insensitive(path->text, base->text, 2)) {
			dynlex_runtime_set_error("A drive-relative path requires an absolute base on the same drive");
			return -1;
		}
		Buffer combined = {0};
		if (buffer_append(&combined, path->text, 2) != 0 || buffer_append(&combined, base->text + 2, base->length - 2) != 0 ||
			(path->length > 2 && buffer_append_byte(&combined, '/') != 0) ||
			buffer_append(&combined, path->text + 2, path->length - 2) != 0) {
			free(combined.data);
			return -1;
		}
		NormalPath normalized;
		int outcome = normalize_path(style, combined.data, combined.length, &normalized);
		free(combined.data);
		if (outcome != 0)
			return -1;
		outcome = copy_normal_path(&normalized, result, result_length);
		free_normal_path(&normalized);
		return outcome;
	}
	return join_normalized(style, base, path, result, result_length);
}

static bool
components_equal(int32_t style, const NormalPath *left, size_t left_index, const NormalPath *right, size_t right_index) {
	size_t length = left->component_lengths[left_index];
	if (length != right->component_lengths[right_index])
		return false;
	const char *left_component = left->text + left->component_offsets[left_index];
	const char *right_component = right->text + right->component_offsets[right_index];
	if (style == DYNLEX_PATH_STYLE_WINDOWS)
		return bytes_equal_ascii_case_insensitive(left_component, right_component, length);
	return memcmp(left_component, right_component, length) == 0;
}

static int
relative_normalized(int32_t style, const NormalPath *path, const NormalPath *base, char **result, size_t *result_length) {
	if (!fully_absolute(style, path) || !fully_absolute(style, base)) {
		dynlex_runtime_set_error("Relative path calculation requires absolute target and base paths");
		return -1;
	}
	if (!roots_equal(style, path, base)) {
		dynlex_runtime_set_error("Target and base paths have different roots");
		return -1;
	}
	size_t common = 0;
	while (common < path->component_count && common < base->component_count &&
		   components_equal(style, path, common, base, common))
		++common;

	Buffer output = {0};
	for (size_t index = common; index < base->component_count; ++index) {
		if (output.length != 0 && buffer_append_byte(&output, '/') != 0)
			goto failure;
		if (buffer_append(&output, "..", 2) != 0)
			goto failure;
	}
	for (size_t index = common; index < path->component_count; ++index) {
		if (output.length != 0 && buffer_append_byte(&output, '/') != 0)
			goto failure;
		if (buffer_append(&output, path->text + path->component_offsets[index], path->component_lengths[index]) != 0)
			goto failure;
	}
	if (output.length == 0 && buffer_append_byte(&output, '.') != 0)
		goto failure;
	*result = output.data;
	*result_length = output.length;
	return 0;
failure:
	free(output.data);
	return -1;
}

static bool uri_safe_byte(uint8_t byte) {
	return (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') || (byte >= '0' && byte <= '9') || byte == '-' ||
		   byte == '.' || byte == '_' || byte == '~' || byte == '/' || byte == ':';
}

static bool uri_unreserved_byte(uint8_t byte) {
	return (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') || (byte >= '0' && byte <= '9') || byte == '-' ||
		   byte == '.' || byte == '_' || byte == '~';
}

static bool uri_subdelimiter_byte(uint8_t byte) {
	return byte == '!' || byte == '$' || byte == '&' || byte == '\'' || byte == '(' || byte == ')' || byte == '*' ||
		   byte == '+' || byte == ',' || byte == ';' || byte == '=';
}

static int append_uri_encoded(Buffer *output, const char *text, size_t length) {
	static const char digits[] = "0123456789ABCDEF";
	for (size_t index = 0; index < length; ++index) {
		uint8_t byte = (uint8_t)text[index];
		if (uri_safe_byte(byte)) {
			if (buffer_append_byte(output, (char)byte) != 0)
				return -1;
		} else {
			char encoded[] = {'%', digits[byte >> 4U], digits[byte & 0x0fU]};
			if (buffer_append(output, encoded, sizeof(encoded)) != 0)
				return -1;
		}
	}
	return 0;
}

static int append_uri_authority(Buffer *output, const char *text, size_t length) {
	static const char digits[] = "0123456789ABCDEF";
	for (size_t index = 0; index < length; ++index) {
		uint8_t byte = (uint8_t)text[index];
		if (uri_unreserved_byte(byte) || uri_subdelimiter_byte(byte)) {
			if (buffer_append_byte(output, (char)byte) != 0)
				return -1;
		} else {
			char encoded[] = {'%', digits[byte >> 4U], digits[byte & 0x0fU]};
			if (buffer_append(output, encoded, sizeof(encoded)) != 0)
				return -1;
		}
	}
	return 0;
}

static int encode_file_uri(int32_t style, const NormalPath *path, char **result, size_t *result_length) {
	if (!fully_absolute(style, path)) {
		dynlex_runtime_set_error("A file URI requires an absolute path");
		return -1;
	}
	if (!dynlex_runtime_is_valid_utf8(path->text, path->length)) {
		dynlex_runtime_set_error("A file URI path must be valid UTF-8");
		return -1;
	}
	Buffer output = {0};
	if (style == DYNLEX_PATH_STYLE_WINDOWS && path->root_kind == ROOT_WINDOWS_UNC) {
		size_t server_end = 2;
		while (server_end < path->root_length && path->text[server_end] != '/')
			++server_end;
		if (buffer_append(&output, "file://", 7) != 0 || append_uri_authority(&output, path->text + 2, server_end - 2) != 0 ||
			append_uri_encoded(&output, path->text + server_end, path->length - server_end) != 0)
			goto failure;
	} else {
		const char *prefix = style == DYNLEX_PATH_STYLE_WINDOWS ? "file:///" : "file://";
		size_t prefix_length = style == DYNLEX_PATH_STYLE_WINDOWS ? 8 : 7;
		if (buffer_append(&output, prefix, prefix_length) != 0 || append_uri_encoded(&output, path->text, path->length) != 0)
			goto failure;
	}
	*result = output.data;
	*result_length = output.length;
	return 0;
failure:
	free(output.data);
	return -1;
}

static int hexadecimal_value(char value) {
	if (value >= '0' && value <= '9')
		return value - '0';
	if (value >= 'A' && value <= 'F')
		return value - 'A' + 10;
	if (value >= 'a' && value <= 'f')
		return value - 'a' + 10;
	return -1;
}

static bool ascii_equal_case_insensitive(const char *text, size_t length, const char *expected) {
	size_t expected_length = strlen(expected);
	return length == expected_length && bytes_equal_ascii_case_insensitive(text, expected, length);
}

static int decode_uri_authority(const char *text, size_t length, Buffer *decoded) {
	for (size_t index = 0; index < length; ++index) {
		uint8_t byte = (uint8_t)text[index];
		if (byte == '%') {
			if (index + 2 >= length) {
				dynlex_runtime_set_error("A file URI authority contains an incomplete percent escape");
				return -1;
			}
			int high = hexadecimal_value(text[index + 1]);
			int low = hexadecimal_value(text[index + 2]);
			if (high < 0 || low < 0) {
				dynlex_runtime_set_error("A file URI authority contains an invalid percent escape");
				return -1;
			}
			byte = (uint8_t)((high << 4) | low);
			index += 2;
			if (byte == 0 || byte == '/' || byte == '\\') {
				dynlex_runtime_set_error("A file URI authority contains an invalid encoded byte");
				return -1;
			}
		} else if (!uri_unreserved_byte(byte) && !uri_subdelimiter_byte(byte)) {
			dynlex_runtime_set_error("A file URI contains an invalid authority");
			return -1;
		}
		if (buffer_append_byte(decoded, (char)byte) != 0)
			return -1;
	}
	if (!dynlex_runtime_is_valid_utf8(decoded->data, decoded->length)) {
		dynlex_runtime_set_error("A file URI authority does not contain valid UTF-8");
		return -1;
	}
	return 0;
}

static int decode_uri_path(int32_t style, const char *text, size_t length, Buffer *decoded) {
	for (size_t index = 0; index < length; ++index) {
		uint8_t byte = (uint8_t)text[index];
		if (byte == '?' || byte == '#') {
			dynlex_runtime_set_error("A file URI must not contain a query or fragment");
			return -1;
		}
		bool encoded = false;
		if (byte == '%') {
			if (index + 2 >= length) {
				dynlex_runtime_set_error("A file URI contains an incomplete percent escape");
				return -1;
			}
			int high = hexadecimal_value(text[index + 1]);
			int low = hexadecimal_value(text[index + 2]);
			if (high < 0 || low < 0) {
				dynlex_runtime_set_error("A file URI contains an invalid percent escape");
				return -1;
			}
			byte = (uint8_t)((high << 4) | low);
			index += 2;
			encoded = true;
		} else if (!uri_unreserved_byte(byte) && !uri_subdelimiter_byte(byte) && byte != '/' && byte != ':' && byte != '@') {
			dynlex_runtime_set_error("A file URI path contains an invalid character");
			return -1;
		}
		if (byte == 0) {
			dynlex_runtime_set_error("A file URI decodes to a null byte");
			return -1;
		}
		if (encoded && byte == '/') {
			dynlex_runtime_set_error("A file URI path must not encode a path separator");
			return -1;
		}
		if (style == DYNLEX_PATH_STYLE_WINDOWS && byte == '\\') {
			dynlex_runtime_set_error("A Windows file URI path must use forward slashes");
			return -1;
		}
		if (buffer_append_byte(decoded, (char)byte) != 0)
			return -1;
	}
	if (!dynlex_runtime_is_valid_utf8(decoded->data, decoded->length)) {
		dynlex_runtime_set_error("A file URI path does not contain valid UTF-8");
		return -1;
	}
	return 0;
}

static int decode_file_uri(int32_t style, const char *uri, size_t uri_length, char **result, size_t *result_length) {
	for (size_t index = 0; index < uri_length; ++index) {
		if ((uint8_t)uri[index] > 0x7fU) {
			dynlex_runtime_set_error("A file URI must percent-encode non-ASCII bytes");
			return -1;
		}
	}
	if (uri_length < 6 || !ascii_equal_case_insensitive(uri, 5, "file:") || uri[5] != '/') {
		dynlex_runtime_set_error("Expected an absolute file URI");
		return -1;
	}
	size_t authority_start = 0;
	size_t authority_length = 0;
	size_t path_start = 5;
	if (uri_length >= 7 && uri[6] == '/') {
		authority_start = 7;
		path_start = authority_start;
		while (path_start < uri_length && uri[path_start] != '/')
			++path_start;
		if (path_start == uri_length) {
			dynlex_runtime_set_error("A file URI requires an absolute path");
			return -1;
		}
		authority_length = path_start - authority_start;
	}
	Buffer authority = {0};
	if (authority_length != 0 && decode_uri_authority(uri + authority_start, authority_length, &authority) != 0) {
		free(authority.data);
		return -1;
	}
	bool local_authority = authority_length == 0 || ascii_equal_case_insensitive(authority.data, authority.length, "localhost");

	Buffer decoded = {0};
	if (decode_uri_path(style, uri + path_start, uri_length - path_start, &decoded) != 0) {
		free(authority.data);
		free(decoded.data);
		return -1;
	}
	Buffer path = {0};
	if (!local_authority) {
		if (buffer_append(&path, "//", 2) != 0 || buffer_append(&path, authority.data, authority.length) != 0 ||
			buffer_append(&path, decoded.data, decoded.length) != 0)
			goto failure;
	} else if (style == DYNLEX_PATH_STYLE_WINDOWS) {
		bool unc_path = decoded.length >= 3 && decoded.data[0] == '/' && decoded.data[1] == '/' && decoded.data[2] != '/';
		bool drive_path = decoded.length >= 4 && decoded.data[0] == '/' && ascii_alpha(decoded.data[1]) &&
						  decoded.data[2] == ':' && decoded.data[3] == '/';
		if (!unc_path && !drive_path) {
			dynlex_runtime_set_error("A local Windows file URI requires an absolute drive or UNC path");
			goto failure;
		}
		size_t prefix_length = unc_path ? 0 : 1;
		if (buffer_append(&path, decoded.data + prefix_length, decoded.length - prefix_length) != 0)
			goto failure;
	} else if (buffer_append(&path, decoded.data, decoded.length) != 0) {
		goto failure;
	}
	free(authority.data);
	free(decoded.data);

	NormalPath normalized;
	if (normalize_path(style, path.data, path.length, &normalized) != 0) {
		free(path.data);
		return -1;
	}
	free(path.data);
	if (!fully_absolute(style, &normalized)) {
		free_normal_path(&normalized);
		dynlex_runtime_set_error("A file URI did not contain an absolute path");
		return -1;
	}
	int outcome = copy_normal_path(&normalized, result, result_length);
	free_normal_path(&normalized);
	return outcome;

failure:
	free(authority.data);
	free(decoded.data);
	free(path.data);
	return -1;
}

static int prepare_owned_output(char **output, size_t *output_length) {
	if (output == NULL || output_length == NULL) {
		dynlex_runtime_set_errno_error("Invalid path output arguments", EINVAL);
		return -1;
	}
	*output = NULL;
	*output_length = 0;
	return 0;
}

size_t dynlex_path_error_message(char *buffer, size_t capacity) { return dynlex_runtime_error_message(buffer, capacity); }

int32_t dynlex_path_native_style(void) {
#ifdef _WIN32
	return DYNLEX_PATH_STYLE_WINDOWS;
#else
	return DYNLEX_PATH_STYLE_POSIX;
#endif
}

int32_t dynlex_path_native_style_supported(void) { return 1; }

int dynlex_path_is_absolute(int32_t style, const char *input, size_t input_length, int32_t *is_absolute) {
	dynlex_runtime_clear_error();
	if (is_absolute == NULL) {
		dynlex_runtime_set_errno_error("Invalid path query output argument", EINVAL);
		return -1;
	}
	*is_absolute = 0;
	NormalPath path;
	if (normalize_path(style, input, input_length, &path) != 0)
		return -1;
	*is_absolute = absolute_path(style, &path) ? 1 : 0;
	free_normal_path(&path);
	return 0;
}

int dynlex_path_unary(
	int32_t operation, int32_t style, const char *input, size_t input_length, char **output, size_t *output_length
) {
	dynlex_runtime_clear_error();
	if (prepare_owned_output(output, output_length) != 0)
		return -1;
	NormalPath path;
	if (normalize_path(style, input, input_length, &path) != 0)
		return -1;
	char *result = NULL;
	size_t result_length = 0;
	int outcome = unary_result(operation, &path, &result, &result_length);
	free_normal_path(&path);
	if (outcome == 0) {
		*output = result;
		*output_length = result_length;
	} else {
		free(result);
	}
	return outcome;
}

int dynlex_path_binary(
	int32_t operation, int32_t style, const char *left, size_t left_length, const char *right, size_t right_length,
	char **output, size_t *output_length
) {
	dynlex_runtime_clear_error();
	if (prepare_owned_output(output, output_length) != 0)
		return -1;
	NormalPath left_path;
	NormalPath right_path;
	if (normalize_path(style, left, left_length, &left_path) != 0)
		return -1;
	if (normalize_path(style, right, right_length, &right_path) != 0) {
		free_normal_path(&left_path);
		return -1;
	}
	char *result = NULL;
	size_t result_length = 0;
	int outcome;
	if (operation == DYNLEX_PATH_JOIN)
		outcome = join_normalized(style, &left_path, &right_path, &result, &result_length);
	else if (operation == DYNLEX_PATH_RESOLVE)
		outcome = resolve_normalized(style, &left_path, &right_path, &result, &result_length);
	else if (operation == DYNLEX_PATH_RELATIVE)
		outcome = relative_normalized(style, &left_path, &right_path, &result, &result_length);
	else {
		dynlex_runtime_set_error("Unknown binary path operation");
		outcome = -1;
	}
	free_normal_path(&left_path);
	free_normal_path(&right_path);
	if (outcome == 0) {
		*output = result;
		*output_length = result_length;
	} else {
		free(result);
	}
	return outcome;
}

int dynlex_path_file_uri(
	int32_t operation, int32_t style, const char *input, size_t input_length, char **output, size_t *output_length
) {
	dynlex_runtime_clear_error();
	if (prepare_owned_output(output, output_length) != 0)
		return -1;
	if (validate_path_input(style, input, input_length) != 0)
		return -1;
	char *result = NULL;
	size_t result_length = 0;
	int outcome;
	if (operation == DYNLEX_FILE_URI_ENCODE) {
		NormalPath path;
		if (normalize_path(style, input, input_length, &path) != 0)
			return -1;
		outcome = encode_file_uri(style, &path, &result, &result_length);
		free_normal_path(&path);
	} else if (operation == DYNLEX_FILE_URI_DECODE) {
		outcome = decode_file_uri(style, input, input_length, &result, &result_length);
	} else {
		dynlex_runtime_set_error("Unknown file URI operation");
		outcome = -1;
	}
	if (outcome == 0) {
		*output = result;
		*output_length = result_length;
	} else {
		free(result);
	}
	return outcome;
}
