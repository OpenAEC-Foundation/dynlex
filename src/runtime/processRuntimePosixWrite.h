#ifndef DYNLEX_PROCESS_RUNTIME_POSIX_WRITE_H
#define DYNLEX_PROCESS_RUNTIME_POSIX_WRITE_H

#include <stddef.h>
#include <sys/types.h>

ssize_t dynlex_posix_write_without_sigpipe(int descriptor, const char *data, size_t length);

#endif
