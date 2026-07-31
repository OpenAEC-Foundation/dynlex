#ifndef DYNLEX_TEST_WINDOWS_H
#define DYNLEX_TEST_WINDOWS_H

#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

typedef int BOOL;
typedef uint32_t DWORD;
typedef void *HANDLE;
typedef wchar_t WCHAR;

typedef struct {
	DWORD dwLowDateTime;
	DWORD dwHighDateTime;
} FILETIME;

typedef union {
	struct {
		DWORD LowPart;
		DWORD HighPart;
	};
	uint64_t QuadPart;
} ULARGE_INTEGER;

#if _WIN32_WINNT >= 0x0602
typedef struct {
	uint64_t VolumeSerialNumber;
	struct {
		unsigned char Identifier[16];
	} FileId;
} FILE_ID_INFO;
#endif

typedef struct {
	DWORD FileAttributes;
	DWORD ReparseTag;
} FILE_ATTRIBUTE_TAG_INFO;

typedef struct {
	int64_t CreationTime;
	int64_t LastAccessTime;
	int64_t LastWriteTime;
	int64_t ChangeTime;
	DWORD FileAttributes;
} FILE_BASIC_INFO;

typedef struct {
	BOOL DeleteFile;
} FILE_DISPOSITION_INFO;

typedef int FILE_INFO_BY_HANDLE_CLASS;

enum { FileBasicInfo = 0, FileAttributeTagInfo = 9, FileDispositionInfo = 4 };
#if _WIN32_WINNT >= 0x0602
enum { FileIdInfo = 18 };
#endif

#define TRUE 1
#define FALSE 0
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#define MAXDWORD UINT32_MAX

#define DELETE UINT32_C(0x00010000)
#define SYNCHRONIZE UINT32_C(0x00100000)
#define FILE_LIST_DIRECTORY UINT32_C(0x00000001)
#define FILE_READ_ATTRIBUTES UINT32_C(0x00000080)
#define FILE_WRITE_ATTRIBUTES UINT32_C(0x00000100)
#define GENERIC_WRITE UINT32_C(0x40000000)

#define FILE_SHARE_READ UINT32_C(0x00000001)
#define FILE_SHARE_WRITE UINT32_C(0x00000002)
#define FILE_SHARE_DELETE UINT32_C(0x00000004)
#define CREATE_NEW 1
#define OPEN_EXISTING 3

#define FILE_ATTRIBUTE_READONLY UINT32_C(0x00000001)
#define FILE_ATTRIBUTE_HIDDEN UINT32_C(0x00000002)
#define FILE_ATTRIBUTE_SYSTEM UINT32_C(0x00000004)
#define FILE_ATTRIBUTE_DIRECTORY UINT32_C(0x00000010)
#define FILE_ATTRIBUTE_ARCHIVE UINT32_C(0x00000020)
#define FILE_ATTRIBUTE_DEVICE UINT32_C(0x00000040)
#define FILE_ATTRIBUTE_NORMAL UINT32_C(0x00000080)
#define FILE_ATTRIBUTE_REPARSE_POINT UINT32_C(0x00000400)
#define FILE_ATTRIBUTE_NOT_CONTENT_INDEXED UINT32_C(0x00002000)
#define FILE_FLAG_OPEN_REPARSE_POINT UINT32_C(0x00200000)
#define FILE_FLAG_BACKUP_SEMANTICS UINT32_C(0x02000000)

#define ERROR_FILE_NOT_FOUND 2
#define ERROR_PATH_NOT_FOUND 3
#define ERROR_WRITE_FAULT 29
#define ERROR_INVALID_NAME 123
#define ERROR_FILE_EXISTS 80
#define ERROR_ALREADY_EXISTS 183

#define _TRUNCATE ((size_t)-1)
#define _countof(value) (sizeof(value) / sizeof((value)[0]))

HANDLE CreateFileW(const WCHAR *, DWORD, DWORD, void *, DWORD, DWORD, HANDLE);
BOOL GetFileInformationByHandleEx(HANDLE, FILE_INFO_BY_HANDLE_CLASS, void *, DWORD);
BOOL SetFileInformationByHandle(HANDLE, FILE_INFO_BY_HANDLE_CLASS, const void *, DWORD);
BOOL SetFileTime(HANDLE, const FILETIME *, const FILETIME *, const FILETIME *);
BOOL WriteFile(HANDLE, const void *, DWORD, DWORD *, void *);
BOOL CloseHandle(HANDLE);
DWORD GetLastError(void);
void SetLastError(DWORD);
DWORD GetCurrentProcessId(void);
wchar_t *_wcsdup(const wchar_t *);
int _snwprintf_s(wchar_t *, size_t, size_t, const wchar_t *, ...);

#endif
