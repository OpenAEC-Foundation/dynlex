#include "hostRuntimeWindowsPath.h"

#include <stddef.h>
#include <wchar.h>

static int check(const wchar_t *path, DynlexWindowsPathNamespace expected) {
	return dynlex_windows_path_namespace(path, wcslen(path)) == expected ? 0 : 1;
}

int main(void) {
	if (check(L"C:\\bin\\app.exe", DYNLEX_WINDOWS_PATH_REGULAR) != 0)
		return 1;
	if (check(L"\\\\server\\share\\app.exe", DYNLEX_WINDOWS_PATH_REGULAR) != 0)
		return 1;
	if (check(L"\\\\?\\C:\\bin\\app.exe", DYNLEX_WINDOWS_PATH_EXTENDED) != 0)
		return 1;
	if (check(L"\\\\?\\UNC\\server\\share\\app.exe", DYNLEX_WINDOWS_PATH_EXTENDED) != 0)
		return 1;
	if (check(L"\\\\.\\device", DYNLEX_WINDOWS_PATH_DEVICE) != 0)
		return 1;
	return 0;
}
