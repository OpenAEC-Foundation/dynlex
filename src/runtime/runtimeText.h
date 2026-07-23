#ifndef DYNLEX_RUNTIME_TEXT_H
#define DYNLEX_RUNTIME_TEXT_H

#include <stdbool.h>
#include <stddef.h>

bool dynlex_runtime_is_valid_utf8(const char *text, size_t length);

#endif
