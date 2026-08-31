#ifndef DYNLEX_HOST_RUNTIME_INTERNAL_H
#define DYNLEX_HOST_RUNTIME_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

int dynlex_platform_executable_path(char **path, size_t *length, int32_t *supported);
int dynlex_platform_user_cache_directory(char **path, size_t *length, int32_t *supported);
int dynlex_platform_prepare_standard_input(void);
int dynlex_platform_environment_value(const char *name, size_t name_length, char **value, size_t *length, int32_t *found);
int dynlex_platform_find_executable(const char *name, size_t name_length, char **path, size_t *length, int32_t *found);
int dynlex_platform_is_administrator(int32_t *administrator);
const char *dynlex_platform_name(void);

#endif
