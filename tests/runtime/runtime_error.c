#define _POSIX_C_SOURCE 200809L

#include "runtimeError.h"

#include <errno.h>
#include <locale.h>
#include <stdio.h>
#include <string.h>

int main(void) {
	char message[512];
	dynlex_runtime_clear_error();
	if (dynlex_runtime_error_message(message, sizeof(message)) != 0 || message[0] != '\0' || errno != 0) {
		fprintf(stderr, "clearing the runtime error did not reset its state\n");
		return 1;
	}

	locale_t original_locale = uselocale((locale_t)0);
	if (original_locale == (locale_t)0) {
		perror("uselocale");
		return 1;
	}
	dynlex_runtime_set_errno_error("opening file", ENOENT);
	if (uselocale((locale_t)0) != original_locale) {
		fprintf(stderr, "formatting an error changed the calling thread's locale\n");
		return 1;
	}
	if (errno != ENOENT) {
		fprintf(stderr, "formatting an error did not preserve errno\n");
		return 1;
	}

	size_t length = dynlex_runtime_error_message(message, sizeof(message));
	if (length != strlen(message) || strncmp(message, "opening file: ", strlen("opening file: ")) != 0) {
		fprintf(stderr, "unexpected runtime error message: %s\n", message);
		return 1;
	}
	char truncated[5];
	if (dynlex_runtime_error_message(truncated, sizeof(truncated)) != length || strcmp(truncated, "open") != 0) {
		fprintf(stderr, "runtime error truncation contract failed\n");
		return 1;
	}
	return 0;
}
