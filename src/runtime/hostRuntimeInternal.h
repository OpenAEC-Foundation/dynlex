#ifndef DYNLEX_HOST_RUNTIME_INTERNAL_H
#define DYNLEX_HOST_RUNTIME_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

int dynlex_platform_executable_path(char **path, size_t *length, int32_t *supported);
int dynlex_platform_prepare_standard_input(void);

#endif
