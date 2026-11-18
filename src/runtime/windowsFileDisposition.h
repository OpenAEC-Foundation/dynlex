#pragma once

#include <windows.h>

#define DYNLEX_FILE_DISPOSITION_IGNORE_READONLY_ATTRIBUTE 0x00000010UL

static inline BOOL dynlex_unlink_windows_file(HANDLE file, DWORD extra_flags) {
	// FileDispositionInfoEx may be absent from the SDK headers at our target version.
	const FILE_INFO_BY_HANDLE_CLASS disposition_info_ex = (FILE_INFO_BY_HANDLE_CLASS)21;
	const DWORD delete_file = 0x00000001UL;
	const DWORD posix_semantics = 0x00000002UL;
	struct {
		DWORD flags;
	} disposition = {delete_file | posix_semantics | extra_flags};
	return SetFileInformationByHandle(file, disposition_info_ex, &disposition, (DWORD)sizeof(disposition));
}
