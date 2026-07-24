#include "hostRuntimeWindowsPath.h"

DynlexWindowsPathNamespace dynlex_windows_path_namespace(const wchar_t *path, size_t length) {
	if (length < 4 || path[0] != L'\\' || path[1] != L'\\' || path[3] != L'\\')
		return DYNLEX_WINDOWS_PATH_REGULAR;
	if (path[2] == L'?')
		return DYNLEX_WINDOWS_PATH_EXTENDED;
	if (path[2] == L'.')
		return DYNLEX_WINDOWS_PATH_DEVICE;
	return DYNLEX_WINDOWS_PATH_REGULAR;
}
