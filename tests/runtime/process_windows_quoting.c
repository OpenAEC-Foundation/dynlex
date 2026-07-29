#include "processRuntimeWindowsQuoting.h"

#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

typedef struct {
	const wchar_t *input;
	const wchar_t *expected;
} QuotingCase;

static int verify_case(size_t index, const QuotingCase *test_case) {
	size_t expected_length = wcslen(test_case->expected);
	size_t reported_length = dynlex_windows_quoted_argument_length(test_case->input);
	if (reported_length != expected_length) {
		fprintf(stderr, "Case %zu reported length %zu instead of %zu\n", index, reported_length, expected_length);
		return -1;
	}
	wchar_t *output = calloc(reported_length + 1, sizeof(*output));
	if (output == NULL) {
		fprintf(stderr, "Could not allocate case %zu output\n", index);
		return -1;
	}
	wchar_t *end = dynlex_windows_append_quoted_argument(output, test_case->input);
	int result = 0;
	if ((size_t)(end - output) != reported_length) {
		fprintf(stderr, "Case %zu wrote an unexpected number of characters\n", index);
		result = -1;
	} else if (wcscmp(output, test_case->expected) != 0) {
		fprintf(stderr, "Case %zu produced unexpected quoting\n", index);
		result = -1;
	}
	free(output);
	return result;
}

int main(void) {
	static const wchar_t empty_expected[] = {L'"', L'"', L'\0'};
	static const wchar_t spaced_input[] = {L't', L'w', L'o', L' ', L'w', L'o', L'r', L'd', L's', L'\0'};
	static const wchar_t spaced_expected[] = {
		L'"', L't', L'w', L'o', L' ', L'w', L'o', L'r', L'd', L's', L'"', L'\0',
	};
	static const wchar_t tabbed_input[] = {L'a', L'\t', L'b', L'\0'};
	static const wchar_t tabbed_expected[] = {L'"', L'a', L'\t', L'b', L'"', L'\0'};
	static const wchar_t quote_input[] = {L'a', L'"', L'b', L'\0'};
	static const wchar_t quote_expected[] = {L'"', L'a', L'\\', L'"', L'b', L'"', L'\0'};
	static const wchar_t slash_input[] = {L'a', L'\\', L'\0'};
	static const wchar_t trailing_slash_input[] = {L'a', L' ', L'\\', L'\0'};
	static const wchar_t trailing_slash_expected[] = {L'"', L'a', L' ', L'\\', L'\\', L'"', L'\0'};
	static const wchar_t slashes_before_quote_input[] = {L'a', L' ', L'\\', L'\\', L'"', L'b', L'\0'};
	static const wchar_t slashes_before_quote_expected[] = {
		L'"', L'a', L' ', L'\\', L'\\', L'\\', L'\\', L'\\', L'"', L'b', L'"', L'\0',
	};
	static const QuotingCase cases[] = {
		{L"plain", L"plain"},
		{L"", empty_expected},
		{spaced_input, spaced_expected},
		{tabbed_input, tabbed_expected},
		{quote_input, quote_expected},
		{slash_input, slash_input},
		{trailing_slash_input, trailing_slash_expected},
		{slashes_before_quote_input, slashes_before_quote_expected},
	};
	for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
		if (verify_case(index, &cases[index]) != 0)
			return 1;
	}
	return 0;
}
