#ifndef DYNLEX_WINDOWS_ENVIRONMENT_H
#define DYNLEX_WINDOWS_ENVIRONMENT_H

#include <stdbool.h>
#include <stddef.h>
#include <wchar.h>

int dynlex_windows_compare_environment_text(const wchar_t *left, size_t left_length, const wchar_t *right, size_t right_length);
bool dynlex_windows_environment_name_matches(const wchar_t *entry, const wchar_t *name);
const wchar_t *dynlex_windows_environment_block_value(const wchar_t *environment, const wchar_t *name);

#endif
