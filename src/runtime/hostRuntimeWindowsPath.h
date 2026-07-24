#ifndef DYNLEX_HOST_RUNTIME_WINDOWS_PATH_H
#define DYNLEX_HOST_RUNTIME_WINDOWS_PATH_H

#include <stddef.h>
#include <wchar.h>

typedef enum {
	DYNLEX_WINDOWS_PATH_REGULAR,
	DYNLEX_WINDOWS_PATH_EXTENDED,
	DYNLEX_WINDOWS_PATH_DEVICE,
} DynlexWindowsPathNamespace;

DynlexWindowsPathNamespace dynlex_windows_path_namespace(const wchar_t *path, size_t length);

#endif
