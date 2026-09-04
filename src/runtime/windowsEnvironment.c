#define WIN32_LEAN_AND_MEAN

#include "windowsEnvironment.h"

#include <limits.h>
#include <stdlib.h>
#include <wchar.h>
#include <windows.h>

int dynlex_windows_compare_environment_text(
	const wchar_t *left, size_t left_length, const wchar_t *right, size_t right_length
) {
	if (left_length > INT_MAX || right_length > INT_MAX)
		abort();
	int result = CompareStringOrdinal(left, (int)left_length, right, (int)right_length, TRUE);
	if (result == 0)
		abort();
	return result - CSTR_EQUAL;
}

bool dynlex_windows_environment_name_matches(const wchar_t *entry, const wchar_t *name) {
	const wchar_t *separator = wcschr(entry, L'=');
	if (separator == NULL)
		return false;
	return dynlex_windows_compare_environment_text(entry, (size_t)(separator - entry), name, wcslen(name)) == 0;
}

const wchar_t *dynlex_windows_environment_block_value(const wchar_t *environment, const wchar_t *name) {
	for (const wchar_t *entry = environment; *entry != L'\0'; entry += wcslen(entry) + 1) {
		if (entry[0] != L'=' && dynlex_windows_environment_name_matches(entry, name))
			return wcschr(entry, L'=') + 1;
	}
	return NULL;
}
