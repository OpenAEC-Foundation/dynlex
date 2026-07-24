#define _POSIX_C_SOURCE 200809L

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int dynlex_path_file_uri(
	int32_t operation, int32_t style, const char *input, size_t input_length, char **output, size_t *output_length,
	int32_t *supported
);

static int write_marker(const char *path, char marker) {
	FILE *file = fopen(path, "wb");
	if (file == NULL)
		return -1;
	int write_succeeded = fputc(marker, file) == marker;
	int close_succeeded = fclose(file) == 0;
	return write_succeeded && close_succeeded ? 0 : -1;
}

int main(void) {
	char temporary[] = "/tmp/dynlex-path-uri-XXXXXX";
	if (mkdtemp(temporary) == NULL)
		return 1;

	char real[512];
	char deep[512];
	char local_target[512];
	char real_target[512];
	char link[512];
	char identity_path[512];
	if (snprintf(real, sizeof(real), "%s/real", temporary) < 0 || snprintf(deep, sizeof(deep), "%s/real/deep", temporary) < 0 ||
		snprintf(local_target, sizeof(local_target), "%s/target", temporary) < 0 ||
		snprintf(real_target, sizeof(real_target), "%s/real/target", temporary) < 0 ||
		snprintf(link, sizeof(link), "%s/link", temporary) < 0 ||
		snprintf(identity_path, sizeof(identity_path), "%s/link/../target", temporary) < 0)
		return 1;
	if (mkdir(real, 0700) != 0 || mkdir(deep, 0700) != 0 || write_marker(local_target, 'L') != 0 ||
		write_marker(real_target, 'R') != 0 || symlink("real/deep", link) != 0)
		return 1;

	char *uri = NULL;
	size_t uri_length = 0;
	int32_t supported = 0;
	if (dynlex_path_file_uri(1, 1, identity_path, strlen(identity_path), &uri, &uri_length, &supported) != 0 || supported != 1)
		return 1;

	char *round_trip = NULL;
	size_t round_trip_length = 0;
	supported = 0;
	if (dynlex_path_file_uri(2, 1, uri, uri_length, &round_trip, &round_trip_length, &supported) != 0 || supported != 1 ||
		round_trip_length != strlen(identity_path) || memcmp(round_trip, identity_path, round_trip_length) != 0)
		return 1;

	FILE *resolved = fopen(round_trip, "rb");
	int marker = resolved == NULL ? EOF : fgetc(resolved);
	if (resolved != NULL)
		fclose(resolved);
	free(uri);
	free(round_trip);

	unlink(link);
	unlink(local_target);
	unlink(real_target);
	rmdir(deep);
	rmdir(real);
	rmdir(temporary);
	return marker == 'R' ? 0 : 1;
}
