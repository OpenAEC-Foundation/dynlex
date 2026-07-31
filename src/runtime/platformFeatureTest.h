#ifndef DYNLEX_PLATFORM_FEATURE_TEST_H
#define DYNLEX_PLATFORM_FEATURE_TEST_H

#if defined(__linux__) && defined(DYNLEX_REQUIRE_GNU_SOURCE) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE
#endif

#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#endif
