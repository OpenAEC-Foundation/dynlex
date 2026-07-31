#define _POSIX_C_SOURCE 200809L

#include "runtimeError.h"

#include <errno.h>
#include <locale.h>
#include <stdio.h>
#include <string.h>

#if defined(_MSC_VER)
#define DYNLEX_THREAD_LOCAL __declspec(thread)
#else
#define DYNLEX_THREAD_LOCAL _Thread_local
#endif

static DYNLEX_THREAD_LOCAL char runtime_error_message[512];

static void copy_error_message(const char *message) {
	size_t length = strlen(message);
	if (length >= sizeof(runtime_error_message))
		length = sizeof(runtime_error_message) - 1;
	memcpy(runtime_error_message, message, length);
	runtime_error_message[length] = '\0';
}

void dynlex_runtime_clear_error(void) {
	runtime_error_message[0] = '\0';
	errno = 0;
}

size_t dynlex_runtime_error_message(char *buffer, size_t capacity) {
	size_t length = strlen(runtime_error_message);
	if (buffer != NULL && capacity > 0) {
		size_t copied = length < capacity - 1 ? length : capacity - 1;
		memcpy(buffer, runtime_error_message, copied);
		buffer[copied] = '\0';
	}
	return length;
}

void dynlex_runtime_set_error(const char *message) {
	if (message == NULL)
		message = "Unknown runtime error";
	copy_error_message(message);
}

void dynlex_runtime_set_errno_error(const char *operation, int error_number) {
	char system_message[320];
	errno = error_number;
#if defined(_WIN32)
	if (strerror_s(system_message, sizeof(system_message), error_number) != 0)
		snprintf(system_message, sizeof(system_message), "C runtime error %d", error_number);
#else
	locale_t english_locale = newlocale(LC_MESSAGES_MASK, "C", (locale_t)0);
	if (english_locale == (locale_t)0)
		snprintf(system_message, sizeof(system_message), "C runtime error %d", error_number);
	else {
		locale_t previous_locale = uselocale(english_locale);
		int message_result = strerror_r(error_number, system_message, sizeof(system_message));
		uselocale(previous_locale);
		freelocale(english_locale);
		if (message_result != 0)
			snprintf(system_message, sizeof(system_message), "C runtime error %d", error_number);
	}
#endif
	snprintf(runtime_error_message, sizeof(runtime_error_message), "%s: %s", operation, system_message);
	errno = error_number;
}

#ifdef _WIN32
void dynlex_runtime_set_windows_error(const char *operation, DWORD error_number) {
	char system_message[320];
	DWORD length = FormatMessageA(
		FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, error_number,
		MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), system_message, (DWORD)sizeof(system_message), NULL
	);
	if (length == 0)
		snprintf(system_message, sizeof(system_message), "Windows error %lu", error_number);
	else {
		while (length > 0 && (system_message[length - 1] == '\r' || system_message[length - 1] == '\n'))
			system_message[--length] = '\0';
	}
	snprintf(runtime_error_message, sizeof(runtime_error_message), "%s: %s", operation, system_message);
}
#endif
