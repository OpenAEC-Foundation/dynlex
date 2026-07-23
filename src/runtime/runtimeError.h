#ifndef DYNLEX_RUNTIME_ERROR_H
#define DYNLEX_RUNTIME_ERROR_H

#include <stddef.h>

#ifdef _WIN32
#include <windows.h>
#endif

void dynlex_runtime_clear_error(void);
size_t dynlex_runtime_error_message(char *buffer, size_t capacity);
void dynlex_runtime_set_error(const char *message);
void dynlex_runtime_set_errno_error(const char *operation, int error_number);

#ifdef _WIN32
void dynlex_runtime_set_windows_error(const char *operation, DWORD error_number);
#endif

#endif
