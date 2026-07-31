#ifndef DYNLEX_FILESYSTEM_STAT_POSIX_H
#define DYNLEX_FILESYSTEM_STAT_POSIX_H

#include "platformFeatureTest.h"

#include <stdint.h>
#include <sys/stat.h>

static inline int64_t dynlex_stat_access_seconds(const struct stat *attributes) {
#if defined(__APPLE__)
	return (int64_t)attributes->st_atimespec.tv_sec;
#else
	return (int64_t)attributes->st_atim.tv_sec;
#endif
}

static inline int32_t dynlex_stat_access_nanoseconds(const struct stat *attributes) {
#if defined(__APPLE__)
	return (int32_t)attributes->st_atimespec.tv_nsec;
#else
	return (int32_t)attributes->st_atim.tv_nsec;
#endif
}

static inline int64_t dynlex_stat_modification_seconds(const struct stat *attributes) {
#if defined(__APPLE__)
	return (int64_t)attributes->st_mtimespec.tv_sec;
#else
	return (int64_t)attributes->st_mtim.tv_sec;
#endif
}

static inline int32_t dynlex_stat_modification_nanoseconds(const struct stat *attributes) {
#if defined(__APPLE__)
	return (int32_t)attributes->st_mtimespec.tv_nsec;
#else
	return (int32_t)attributes->st_mtim.tv_nsec;
#endif
}

#endif
