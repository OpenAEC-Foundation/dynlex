#ifndef DYNLEX_PROCESS_RUNTIME_WINDOWS_QUOTING_H
#define DYNLEX_PROCESS_RUNTIME_WINDOWS_QUOTING_H

#include <stddef.h>
#include <wchar.h>

size_t dynlex_windows_quoted_argument_length(const wchar_t *argument);
wchar_t *dynlex_windows_append_quoted_argument(wchar_t *destination, const wchar_t *argument);

#endif
