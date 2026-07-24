#include "pathRuntimeInternal.h"
#include "runtimeError.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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

typedef struct {
	const char *data;
	size_t length;
} Component;

bool dynlex_path_ascii_alpha(char value) { return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z'); }

char dynlex_path_ascii_lower(char value) { return value >= 'A' && value <= 'Z' ? (char)(value + ('a' - 'A')) : value; }

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

int dynlex_path_buffer_append(Buffer *buffer, const char *data, size_t length) {
	if (buffer_reserve(buffer, length) != 0)
		return -1;
	if (length != 0)
		memcpy(buffer->data + buffer->length, data, length);
	buffer->length += length;
	return 0;
}

int dynlex_path_buffer_append_byte(Buffer *buffer, char value) { return dynlex_path_buffer_append(buffer, &value, 1); }

void dynlex_path_free(ParsedPath *path) {
	free(path->text);
	free(path->component_offsets);
	free(path->component_lengths);
	memset(path, 0, sizeof(*path));
}

int dynlex_path_validate_input(int32_t style, const char *text, size_t length) {
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
		return dynlex_path_buffer_append(output, slash_count == 2 ? "//" : "/", slash_count == 2 ? 2 : 1);
	}

	*root_kind = ROOT_WINDOWS_RELATIVE;
	if (length >= 4 && windows_separator(text[0]) && windows_separator(text[1]) && (text[2] == '?' || text[2] == '.') &&
		windows_separator(text[3])) {
		dynlex_runtime_set_error("Windows device namespace paths are not supported");
		return -1;
	}
	if (length >= 2 && dynlex_path_ascii_alpha(text[0]) && text[1] == ':') {
		if (dynlex_path_buffer_append(output, text, 2) != 0)
			return -1;
		*cursor = 2;
		if (*cursor < length && windows_separator(text[*cursor])) {
			*root_kind = ROOT_WINDOWS_DRIVE_ABSOLUTE;
			if (dynlex_path_buffer_append_byte(output, '/') != 0)
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
			return dynlex_path_buffer_append_byte(output, '/');
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
		if (dynlex_path_buffer_append(output, "//", 2) != 0 ||
			dynlex_path_buffer_append(output, text + server_start, server_end - server_start) != 0 ||
			dynlex_path_buffer_append_byte(output, '/') != 0 ||
			dynlex_path_buffer_append(output, text + share_start, share_end - share_start) != 0)
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

int dynlex_path_parse(int32_t style, const char *text, size_t length, bool normalize_dot_segments, ParsedPath *parsed) {
	memset(parsed, 0, sizeof(*parsed));
	if (dynlex_path_validate_input(style, text, length) != 0)
		return -1;

	Buffer output = {0};
	Component *components = NULL;
	if (allocate_components(length, &components) != 0)
		return -1;
	size_t cursor;
	if (append_root(style, text, length, &cursor, &parsed->root_kind, &output) != 0) {
		free(components);
		free(output.data);
		return -1;
	}
	parsed->root_length = output.length;

	size_t component_count = 0;
	while (cursor < length) {
		while (cursor < length && path_separator(style, text[cursor]))
			++cursor;
		size_t start = cursor;
		while (cursor < length && !path_separator(style, text[cursor]))
			++cursor;
		Component component = {text + start, cursor - start};
		if (component.length == 0)
			continue;
		if (normalize_dot_segments && component_equals(&component, "."))
			continue;
		if (normalize_dot_segments && component_equals(&component, "..")) {
			if (component_count != 0 && !component_equals(&components[component_count - 1], "..")) {
				--component_count;
			} else if (!root_is_directory(parsed->root_kind)) {
				components[component_count++] = component;
			}
			continue;
		}
		components[component_count++] = component;
	}

	parsed->component_offsets = malloc((component_count == 0 ? 1 : component_count) * sizeof(size_t));
	parsed->component_lengths = malloc((component_count == 0 ? 1 : component_count) * sizeof(size_t));
	if (parsed->component_offsets == NULL || parsed->component_lengths == NULL) {
		free(components);
		dynlex_path_free(parsed);
		free(output.data);
		dynlex_runtime_set_errno_error("Could not allocate path component index", ENOMEM);
		return -1;
	}
	for (size_t index = 0; index < component_count; ++index) {
		bool drive_first = parsed->root_kind == ROOT_WINDOWS_DRIVE_RELATIVE && index == 0;
		if (output.length != 0 && output.data[output.length - 1] != '/' && !drive_first) {
			if (dynlex_path_buffer_append_byte(&output, '/') != 0)
				goto failure;
		}
		parsed->component_offsets[index] = output.length;
		parsed->component_lengths[index] = components[index].length;
		if (dynlex_path_buffer_append(&output, components[index].data, components[index].length) != 0)
			goto failure;
	}
	free(components);
	if (output.length == 0 && dynlex_path_buffer_append_byte(&output, '.') != 0)
		goto failure_without_components;
	parsed->text = output.data;
	parsed->length = output.length;
	parsed->component_count = component_count;
	return 0;

failure:
	free(components);
failure_without_components:
	free(output.data);
	dynlex_path_free(parsed);
	return -1;
}

int dynlex_path_copy_bytes(const char *data, size_t length, char **result, size_t *result_length) {
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

int dynlex_path_copy(const ParsedPath *path, char **result, size_t *result_length) {
	return dynlex_path_copy_bytes(path->text, path->length, result, result_length);
}

static bool bytes_equal_ascii_case_insensitive(const char *left, const char *right, size_t length) {
	for (size_t index = 0; index < length; ++index) {
		if (dynlex_path_ascii_lower(left[index]) != dynlex_path_ascii_lower(right[index]))
			return false;
	}
	return true;
}

static bool roots_equal(int32_t style, const ParsedPath *left, const ParsedPath *right) {
	if (left->root_kind != right->root_kind || left->root_length != right->root_length)
		return false;
	if (style == DYNLEX_PATH_STYLE_WINDOWS)
		return bytes_equal_ascii_case_insensitive(left->text, right->text, left->root_length);
	return memcmp(left->text, right->text, left->root_length) == 0;
}

bool dynlex_path_fully_absolute(int32_t style, const ParsedPath *path) {
	if (style == DYNLEX_PATH_STYLE_POSIX)
		return path->root_kind == ROOT_POSIX_SINGLE || path->root_kind == ROOT_POSIX_DOUBLE;
	return path->root_kind == ROOT_WINDOWS_DRIVE_ABSOLUTE || path->root_kind == ROOT_WINDOWS_UNC;
}

static bool absolute_path(int32_t style, const ParsedPath *path) {
	return dynlex_path_fully_absolute(style, path) ||
		   (style == DYNLEX_PATH_STYLE_WINDOWS && path->root_kind == ROOT_WINDOWS_ROOTED);
}

static int concatenate_and_normalize(
	int32_t style, const char *left, size_t left_length, bool separator, const char *right, size_t right_length, char **result,
	size_t *result_length
) {
	Buffer combined = {0};
	if (dynlex_path_buffer_append(&combined, left, left_length) != 0 ||
		(separator && dynlex_path_buffer_append_byte(&combined, '/') != 0) ||
		dynlex_path_buffer_append(&combined, right, right_length) != 0) {
		free(combined.data);
		return -1;
	}
	ParsedPath normalized;
	int outcome = dynlex_path_parse(style, combined.data, combined.length, true, &normalized);
	free(combined.data);
	if (outcome != 0)
		return -1;
	outcome = dynlex_path_copy(&normalized, result, result_length);
	dynlex_path_free(&normalized);
	return outcome;
}

static int
join_normalized(int32_t style, const ParsedPath *left, const ParsedPath *right, char **result, size_t *result_length) {
	if (dynlex_path_fully_absolute(style, right))
		return dynlex_path_copy(right, result, result_length);
	if (style == DYNLEX_PATH_STYLE_WINDOWS && right->root_kind == ROOT_WINDOWS_ROOTED) {
		if (left->root_length == 0 || left->root_kind == ROOT_WINDOWS_RELATIVE)
			return dynlex_path_copy(right, result, result_length);
		return concatenate_and_normalize(
			style, left->text, left->root_length, false, right->text, right->length, result, result_length
		);
	}
	if (style == DYNLEX_PATH_STYLE_WINDOWS && right->root_kind == ROOT_WINDOWS_DRIVE_RELATIVE) {
		bool left_has_same_drive =
			(left->root_kind == ROOT_WINDOWS_DRIVE_RELATIVE || left->root_kind == ROOT_WINDOWS_DRIVE_ABSOLUTE) &&
			bytes_equal_ascii_case_insensitive(left->text, right->text, 2);
		if (!left_has_same_drive)
			return dynlex_path_copy(right, result, result_length);
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
		return dynlex_path_copy(right, result, result_length);
	if (right_is_dot)
		return dynlex_path_copy(left, result, result_length);
	return concatenate_and_normalize(style, left->text, left->length, true, right->text, right->length, result, result_length);
}

static int parent_path(const ParsedPath *path, char **result, size_t *result_length) {
	if (path->component_count == 0)
		return dynlex_path_copy(path, result, result_length);
	size_t parent_length = path->component_offsets[path->component_count - 1];
	if (parent_length > path->root_length && path->text[parent_length - 1] == '/')
		--parent_length;
	if (parent_length == 0)
		return dynlex_path_copy_bytes(".", 1, result, result_length);
	return dynlex_path_copy_bytes(path->text, parent_length, result, result_length);
}

static void filename_span(const ParsedPath *path, const char **filename, size_t *filename_length) {
	if (path->component_count == 0) {
		*filename = NULL;
		*filename_length = 0;
		return;
	}
	size_t index = path->component_count - 1;
	*filename = path->text + path->component_offsets[index];
	*filename_length = path->component_lengths[index];
}

static int unary_result(int32_t operation, const ParsedPath *path, char **result, size_t *result_length) {
	if (operation == DYNLEX_PATH_NORMALIZE)
		return dynlex_path_copy(path, result, result_length);
	if (operation == DYNLEX_PATH_PARENT)
		return parent_path(path, result, result_length);

	const char *filename;
	size_t filename_length;
	filename_span(path, &filename, &filename_length);
	if (operation == DYNLEX_PATH_FILENAME)
		return dynlex_path_copy_bytes(filename, filename_length, result, result_length);
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
		return dynlex_path_copy_bytes(filename, suffix_offset, result, result_length);
	if (operation == DYNLEX_PATH_SUFFIX)
		return dynlex_path_copy_bytes(
			filename == NULL ? NULL : filename + suffix_offset, filename_length - suffix_offset, result, result_length
		);
	dynlex_runtime_set_error("Unknown unary path operation");
	return -1;
}

static int
resolve_normalized(int32_t style, const ParsedPath *path, const ParsedPath *base, char **result, size_t *result_length) {
	if (!dynlex_path_fully_absolute(style, base)) {
		dynlex_runtime_set_error("Path resolution requires an absolute base path");
		return -1;
	}
	if (dynlex_path_fully_absolute(style, path))
		return dynlex_path_copy(path, result, result_length);
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
		if (dynlex_path_buffer_append(&combined, path->text, 2) != 0 ||
			dynlex_path_buffer_append(&combined, base->text + 2, base->length - 2) != 0 ||
			(path->length > 2 && dynlex_path_buffer_append_byte(&combined, '/') != 0) ||
			dynlex_path_buffer_append(&combined, path->text + 2, path->length - 2) != 0) {
			free(combined.data);
			return -1;
		}
		ParsedPath normalized;
		int outcome = dynlex_path_parse(style, combined.data, combined.length, true, &normalized);
		free(combined.data);
		if (outcome != 0)
			return -1;
		outcome = dynlex_path_copy(&normalized, result, result_length);
		dynlex_path_free(&normalized);
		return outcome;
	}
	return join_normalized(style, base, path, result, result_length);
}

static bool
components_equal(int32_t style, const ParsedPath *left, size_t left_index, const ParsedPath *right, size_t right_index) {
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
relative_normalized(int32_t style, const ParsedPath *path, const ParsedPath *base, char **result, size_t *result_length) {
	if (!dynlex_path_fully_absolute(style, path) || !dynlex_path_fully_absolute(style, base)) {
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
		if (output.length != 0 && dynlex_path_buffer_append_byte(&output, '/') != 0)
			goto failure;
		if (dynlex_path_buffer_append(&output, "..", 2) != 0)
			goto failure;
	}
	for (size_t index = common; index < path->component_count; ++index) {
		if (output.length != 0 && dynlex_path_buffer_append_byte(&output, '/') != 0)
			goto failure;
		if (dynlex_path_buffer_append(&output, path->text + path->component_offsets[index], path->component_lengths[index]) !=
			0)
			goto failure;
	}
	if (output.length == 0 && dynlex_path_buffer_append_byte(&output, '.') != 0)
		goto failure;
	*result = output.data;
	*result_length = output.length;
	return 0;
failure:
	free(output.data);
	return -1;
}

int dynlex_path_prepare_owned_output(char **output, size_t *output_length) {
	if (output == NULL || output_length == NULL) {
		dynlex_runtime_set_errno_error("Invalid path output arguments", EINVAL);
		return -1;
	}
	*output = NULL;
	*output_length = 0;
	return 0;
}

size_t dynlex_path_error_message(char *buffer, size_t capacity) { return dynlex_runtime_error_message(buffer, capacity); }

int dynlex_path_native_style(int32_t *style, int32_t *supported) {
	dynlex_runtime_clear_error();
	if (style == NULL || supported == NULL) {
		dynlex_runtime_set_errno_error("Invalid native path style result arguments", EINVAL);
		return -1;
	}
#ifdef _WIN32
	*style = DYNLEX_PATH_STYLE_WINDOWS;
#else
	*style = DYNLEX_PATH_STYLE_POSIX;
#endif
	*supported = 1;
	return 0;
}

int dynlex_path_is_absolute(int32_t style, const char *input, size_t input_length, int32_t *is_absolute) {
	dynlex_runtime_clear_error();
	if (is_absolute == NULL) {
		dynlex_runtime_set_errno_error("Invalid path query output argument", EINVAL);
		return -1;
	}
	*is_absolute = 0;
	ParsedPath path;
	if (dynlex_path_parse(style, input, input_length, true, &path) != 0)
		return -1;
	*is_absolute = absolute_path(style, &path) ? 1 : 0;
	dynlex_path_free(&path);
	return 0;
}

int dynlex_path_unary(
	int32_t operation, int32_t style, const char *input, size_t input_length, char **output, size_t *output_length
) {
	dynlex_runtime_clear_error();
	if (dynlex_path_prepare_owned_output(output, output_length) != 0)
		return -1;
	ParsedPath path;
	if (dynlex_path_parse(style, input, input_length, operation == DYNLEX_PATH_NORMALIZE, &path) != 0)
		return -1;
	char *result = NULL;
	size_t result_length = 0;
	int outcome = unary_result(operation, &path, &result, &result_length);
	dynlex_path_free(&path);
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
	if (dynlex_path_prepare_owned_output(output, output_length) != 0)
		return -1;
	ParsedPath left_path;
	ParsedPath right_path;
	if (dynlex_path_parse(style, left, left_length, true, &left_path) != 0)
		return -1;
	if (dynlex_path_parse(style, right, right_length, true, &right_path) != 0) {
		dynlex_path_free(&left_path);
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
	dynlex_path_free(&left_path);
	dynlex_path_free(&right_path);
	if (outcome == 0) {
		*output = result;
		*output_length = result_length;
	} else {
		free(result);
	}
	return outcome;
}
