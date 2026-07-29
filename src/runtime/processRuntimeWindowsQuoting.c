#include "processRuntimeWindowsQuoting.h"

#include <stdbool.h>
#include <string.h>

static bool argument_needs_quotes(const wchar_t *argument) {
	if (*argument == L'\0')
		return true;
	for (const wchar_t *cursor = argument; *cursor != L'\0'; ++cursor) {
		if (*cursor == L' ' || *cursor == L'\t' || *cursor == L'"')
			return true;
	}
	return false;
}

size_t dynlex_windows_quoted_argument_length(const wchar_t *argument) {
	if (!argument_needs_quotes(argument))
		return wcslen(argument);
	size_t length = 2;
	size_t backslashes = 0;
	for (const wchar_t *cursor = argument;; ++cursor) {
		if (*cursor == L'\\') {
			backslashes++;
			continue;
		}
		if (*cursor == L'"')
			length += backslashes * 2 + 2;
		else if (*cursor == L'\0') {
			length += backslashes * 2;
			return length;
		} else
			length += backslashes + 1;
		backslashes = 0;
	}
}

wchar_t *dynlex_windows_append_quoted_argument(wchar_t *destination, const wchar_t *argument) {
	if (!argument_needs_quotes(argument)) {
		size_t length = wcslen(argument);
		memcpy(destination, argument, length * sizeof(wchar_t));
		return destination + length;
	}
	*destination++ = L'"';
	size_t backslashes = 0;
	for (const wchar_t *cursor = argument;; ++cursor) {
		if (*cursor == L'\\') {
			backslashes++;
			continue;
		}
		size_t copies = backslashes;
		if (*cursor == L'"' || *cursor == L'\0')
			copies *= 2;
		for (size_t index = 0; index < copies; ++index)
			*destination++ = L'\\';
		if (*cursor == L'\0')
			break;
		if (*cursor == L'"')
			*destination++ = L'\\';
		*destination++ = *cursor;
		backslashes = 0;
	}
	*destination++ = L'"';
	return destination;
}
