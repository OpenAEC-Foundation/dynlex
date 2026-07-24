#include "pathRuntimeInternal.h"

#include "runtimeError.h"
#include "runtimeText.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum {
	DYNLEX_FILE_URI_ENCODE = 1,
	DYNLEX_FILE_URI_DECODE = 2,
};

static bool uri_unreserved_byte(uint8_t byte) {
	return (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') || (byte >= '0' && byte <= '9') || byte == '-' ||
		   byte == '.' || byte == '_' || byte == '~';
}

static bool uri_subdelimiter_byte(uint8_t byte) {
	return byte == '!' || byte == '$' || byte == '&' || byte == '\'' || byte == '(' || byte == ')' || byte == '*' ||
		   byte == '+' || byte == ',' || byte == ';' || byte == '=';
}

static bool hexadecimal_byte(uint8_t byte) {
	return (byte >= '0' && byte <= '9') || (byte >= 'A' && byte <= 'F') || (byte >= 'a' && byte <= 'f');
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
	if (length != expected_length)
		return false;
	for (size_t index = 0; index < length; ++index) {
		if (dynlex_path_ascii_lower(text[index]) != dynlex_path_ascii_lower(expected[index]))
			return false;
	}
	return true;
}

static bool valid_ipv4_address(const char *text, size_t length) {
	size_t cursor = 0;
	for (size_t part = 0; part < 4; ++part) {
		size_t start = cursor;
		unsigned value = 0;
		while (cursor < length && text[cursor] >= '0' && text[cursor] <= '9') {
			value = value * 10U + (unsigned)(text[cursor] - '0');
			++cursor;
		}
		size_t digits = cursor - start;
		if (digits == 0 || digits > 3 || value > 255U || (digits > 1 && text[start] == '0'))
			return false;
		if (part == 3)
			return cursor == length;
		if (cursor == length || text[cursor] != '.')
			return false;
		++cursor;
	}
	return false;
}

static bool parse_ipv6_side(const char *text, size_t length, bool allow_ipv4, size_t *group_count) {
	*group_count = 0;
	if (length == 0)
		return true;
	size_t cursor = 0;
	while (cursor < length) {
		size_t start = cursor;
		while (cursor < length && text[cursor] != ':')
			++cursor;
		size_t segment_length = cursor - start;
		if (segment_length == 0)
			return false;
		if (memchr(text + start, '.', segment_length) != NULL) {
			if (!allow_ipv4 || cursor != length || !valid_ipv4_address(text + start, segment_length))
				return false;
			*group_count += 2;
		} else {
			if (segment_length > 4)
				return false;
			for (size_t index = start; index < cursor; ++index) {
				if (!hexadecimal_byte((uint8_t)text[index]))
					return false;
			}
			++*group_count;
		}
		if (cursor < length)
			++cursor;
	}
	return true;
}

static bool valid_ipv6_address(const char *text, size_t length) {
	size_t double_colon = SIZE_MAX;
	for (size_t index = 0; index + 1 < length; ++index) {
		if (text[index] == ':' && text[index + 1] == ':') {
			if (double_colon != SIZE_MAX)
				return false;
			double_colon = index;
			++index;
		}
	}
	size_t left_groups = 0;
	size_t right_groups = 0;
	if (double_colon == SIZE_MAX)
		return parse_ipv6_side(text, length, true, &left_groups) && left_groups == 8;
	if (!parse_ipv6_side(text, double_colon, false, &left_groups) ||
		!parse_ipv6_side(text + double_colon + 2, length - double_colon - 2, true, &right_groups))
		return false;
	return left_groups + right_groups < 8;
}

static bool valid_ipv_future(const char *text, size_t length) {
	if (length < 4 || (text[0] != 'v' && text[0] != 'V'))
		return false;
	size_t cursor = 1;
	while (cursor < length && hexadecimal_byte((uint8_t)text[cursor]))
		++cursor;
	if (cursor == 1 || cursor == length || text[cursor] != '.')
		return false;
	++cursor;
	size_t address_start = cursor;
	while (cursor < length) {
		uint8_t byte = (uint8_t)text[cursor++];
		if (!uri_unreserved_byte(byte) && !uri_subdelimiter_byte(byte) && byte != ':')
			return false;
	}
	return cursor > address_start;
}

static bool valid_ip_literal(const char *text, size_t length) {
	return valid_ipv6_address(text, length) || valid_ipv_future(text, length);
}

static bool bracketed_ip_literal(const char *text, size_t length) {
	return length >= 4 && text[0] == '[' && text[length - 1] == ']' && valid_ip_literal(text + 1, length - 2);
}

static int append_percent_encoded(Buffer *output, const char *text, size_t length, bool authority) {
	static const char digits[] = "0123456789ABCDEF";
	for (size_t index = 0; index < length; ++index) {
		uint8_t byte = (uint8_t)text[index];
		bool safe = authority ? uri_unreserved_byte(byte) || uri_subdelimiter_byte(byte)
							  : uri_unreserved_byte(byte) || byte == '/' || byte == ':';
		if (safe) {
			if (dynlex_path_buffer_append_byte(output, (char)byte) != 0)
				return -1;
		} else {
			char encoded[] = {'%', digits[byte >> 4U], digits[byte & 0x0fU]};
			if (dynlex_path_buffer_append(output, encoded, sizeof(encoded)) != 0)
				return -1;
		}
	}
	return 0;
}

static int append_uri_authority(Buffer *output, const char *text, size_t length) {
	if (bracketed_ip_literal(text, length))
		return dynlex_path_buffer_append(output, text, length);
	return append_percent_encoded(output, text, length, true);
}

static int encode_file_uri(int32_t style, const ParsedPath *path, char **result, size_t *result_length) {
	if (!dynlex_path_fully_absolute(style, path)) {
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
		if (dynlex_path_buffer_append(&output, "file://", 7) != 0 ||
			append_uri_authority(&output, path->text + 2, server_end - 2) != 0 ||
			append_percent_encoded(&output, path->text + server_end, path->length - server_end, false) != 0)
			goto failure;
	} else {
		const char *prefix = style == DYNLEX_PATH_STYLE_WINDOWS ? "file:///" : "file://";
		size_t prefix_length = style == DYNLEX_PATH_STYLE_WINDOWS ? 8 : 7;
		if (dynlex_path_buffer_append(&output, prefix, prefix_length) != 0 ||
			append_percent_encoded(&output, path->text, path->length, false) != 0)
			goto failure;
	}
	*result = output.data;
	*result_length = output.length;
	return 0;

failure:
	free(output.data);
	return -1;
}

static int decode_percent(const char *text, size_t length, size_t *index, const char *component, uint8_t *byte) {
	if (*index + 2 >= length) {
		dynlex_runtime_set_error(component);
		return -1;
	}
	int high = hexadecimal_value(text[*index + 1]);
	int low = hexadecimal_value(text[*index + 2]);
	if (high < 0 || low < 0) {
		dynlex_runtime_set_error(component);
		return -1;
	}
	*byte = (uint8_t)((high << 4) | low);
	*index += 2;
	return 0;
}

static int decode_uri_authority(const char *text, size_t length, Buffer *decoded) {
	if (length != 0 && text[0] == '[') {
		if (!bracketed_ip_literal(text, length)) {
			dynlex_runtime_set_error("A file URI contains an invalid IP-literal authority");
			return -1;
		}
		return dynlex_path_buffer_append(decoded, text, length);
	}
	for (size_t index = 0; index < length; ++index) {
		uint8_t byte = (uint8_t)text[index];
		if (byte == '%') {
			if (decode_percent(text, length, &index, "A file URI authority contains an invalid percent escape", &byte) != 0)
				return -1;
			if (byte == 0 || byte == '/' || byte == '\\') {
				dynlex_runtime_set_error("A file URI authority contains an invalid encoded byte");
				return -1;
			}
		} else if (!uri_unreserved_byte(byte) && !uri_subdelimiter_byte(byte)) {
			dynlex_runtime_set_error("A file URI contains an invalid authority");
			return -1;
		}
		if (dynlex_path_buffer_append_byte(decoded, (char)byte) != 0)
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
			if (decode_percent(text, length, &index, "A file URI path contains an invalid percent escape", &byte) != 0)
				return -1;
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
		if (dynlex_path_buffer_append_byte(decoded, (char)byte) != 0)
			return -1;
	}
	if (!dynlex_runtime_is_valid_utf8(decoded->data, decoded->length)) {
		dynlex_runtime_set_error("A file URI path does not contain valid UTF-8");
		return -1;
	}
	return 0;
}

static int
decode_file_uri(int32_t style, const char *uri, size_t uri_length, char **result, size_t *result_length, int32_t *supported) {
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
	if (decode_uri_path(style, uri + path_start, uri_length - path_start, &decoded) != 0)
		goto failure;
	if (!local_authority && style == DYNLEX_PATH_STYLE_POSIX) {
		dynlex_runtime_set_error("A non-local file URI cannot be mapped to a POSIX path");
		*supported = 0;
		free(authority.data);
		free(decoded.data);
		return 0;
	}

	Buffer path = {0};
	if (!local_authority) {
		if (dynlex_path_buffer_append(&path, "//", 2) != 0 ||
			dynlex_path_buffer_append(&path, authority.data, authority.length) != 0 ||
			dynlex_path_buffer_append(&path, decoded.data, decoded.length) != 0)
			goto failure_with_path;
	} else if (style == DYNLEX_PATH_STYLE_WINDOWS) {
		bool unc_path = decoded.length >= 3 && decoded.data[0] == '/' && decoded.data[1] == '/' && decoded.data[2] != '/';
		bool drive_path = decoded.length >= 4 && decoded.data[0] == '/' && dynlex_path_ascii_alpha(decoded.data[1]) &&
						  decoded.data[2] == ':' && decoded.data[3] == '/';
		if (!unc_path && !drive_path) {
			dynlex_runtime_set_error("A local Windows file URI requires an absolute drive or UNC path");
			goto failure_with_path;
		}
		size_t prefix_length = unc_path ? 0 : 1;
		if (dynlex_path_buffer_append(&path, decoded.data + prefix_length, decoded.length - prefix_length) != 0)
			goto failure_with_path;
	} else if (dynlex_path_buffer_append(&path, decoded.data, decoded.length) != 0) {
		goto failure_with_path;
	}
	free(authority.data);
	free(decoded.data);

	ParsedPath parsed;
	if (dynlex_path_parse(style, path.data, path.length, false, &parsed) != 0) {
		free(path.data);
		return -1;
	}
	free(path.data);
	if (!dynlex_path_fully_absolute(style, &parsed)) {
		dynlex_path_free(&parsed);
		dynlex_runtime_set_error("A file URI did not contain an absolute path");
		return -1;
	}
	int outcome = dynlex_path_copy(&parsed, result, result_length);
	dynlex_path_free(&parsed);
	return outcome;

failure_with_path:
	free(path.data);
failure:
	free(authority.data);
	free(decoded.data);
	return -1;
}

int dynlex_path_file_uri(
	int32_t operation, int32_t style, const char *input, size_t input_length, char **output, size_t *output_length,
	int32_t *supported
) {
	dynlex_runtime_clear_error();
	if (supported == NULL) {
		dynlex_runtime_set_errno_error("Invalid file URI support result argument", EINVAL);
		return -1;
	}
	*supported = 1;
	if (dynlex_path_prepare_owned_output(output, output_length) != 0 ||
		dynlex_path_validate_input(style, input, input_length) != 0)
		return -1;

	char *result = NULL;
	size_t result_length = 0;
	int outcome;
	if (operation == DYNLEX_FILE_URI_ENCODE) {
		ParsedPath path;
		if (dynlex_path_parse(style, input, input_length, false, &path) != 0)
			return -1;
		outcome = encode_file_uri(style, &path, &result, &result_length);
		dynlex_path_free(&path);
	} else if (operation == DYNLEX_FILE_URI_DECODE) {
		outcome = decode_file_uri(style, input, input_length, &result, &result_length, supported);
	} else {
		dynlex_runtime_set_error("Unknown file URI operation");
		outcome = -1;
	}
	if (outcome == 0 && *supported != 0) {
		*output = result;
		*output_length = result_length;
	} else {
		free(result);
	}
	return outcome;
}
