#define _POSIX_C_SOURCE 200809L

#include "hostRuntimeInternal.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int expect_cache_directory(const char *xdg_cache_home, const char *home, const char *expected) {
	if (xdg_cache_home == NULL) {
		if (unsetenv("XDG_CACHE_HOME") != 0)
			return 1;
	} else if (setenv("XDG_CACHE_HOME", xdg_cache_home, 1) != 0) {
		return 1;
	}
	if (setenv("HOME", home, 1) != 0)
		return 1;

	char *path = NULL;
	size_t length = 0;
	int32_t supported = 0;
	if (dynlex_platform_user_cache_directory(&path, &length, &supported) != 0)
		return 1;
	if (supported != 1 || path == NULL || length != strlen(expected) || memcmp(path, expected, length) != 0) {
		free(path);
		return 1;
	}
	free(path);
	return 0;
}

int main(void) {
	if (expect_cache_directory("/tmp/dynlex-xdg-cache", "/tmp/dynlex-home", "/tmp/dynlex-xdg-cache") != 0)
		return 1;
#if defined(__APPLE__)
	if (expect_cache_directory("relative-cache", "/tmp/dynlex-home", "/tmp/dynlex-home/Library/Caches") != 0)
		return 2;
#else
	if (expect_cache_directory("relative-cache", "/tmp/dynlex-home", "/tmp/dynlex-home/.cache") != 0)
		return 2;
#endif
	return 0;
}
