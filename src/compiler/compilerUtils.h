#pragma once
#include <cassert>
#include <cstdlib>
#include <iostream>

// Compiler detection
#if defined(__clang__) || defined(__GNUC__)
#define COMPILER_HAS_BUILTIN_UNREACHABLE 1
#elif defined(_MSC_VER)
#define COMPILER_HAS_ASSUME 1
#endif

// C++23 has std::unreachable, but provide fallback for older compilers
#if __cplusplus >= 202302L && __has_include(<utility>)
#include <utility>
#define COMPILER_HAS_STD_UNREACHABLE 1
#endif

/**
 * ASSERT_UNREACHABLE(msg) - Assert in debug, mark unreachable in release
 *
 * Use this for code paths that should never be reached due to prior validation.
 * - Debug builds: Asserts with message, aborts if triggered
 * - Release builds: Tells compiler this code is unreachable for optimization
 * - If somehow reached in release: Terminates immediately (fail hard)
 *
 * Example:
 *   switch (type) {
 *     case Type::Valid: return handleValid();
 *     default: ASSERT_UNREACHABLE("Invalid type after validation");
 *   }
 */
#ifdef NDEBUG
// Release build: Mark as unreachable
#if defined(COMPILER_HAS_STD_UNREACHABLE)
#define ASSERT_UNREACHABLE(msg) std::unreachable()
#elif defined(COMPILER_HAS_BUILTIN_UNREACHABLE)
#define ASSERT_UNREACHABLE(msg) __builtin_unreachable()
#elif defined(COMPILER_HAS_ASSUME)
#define ASSERT_UNREACHABLE(msg) __assume(0)
#else
// Fallback: print message then terminate
#define ASSERT_UNREACHABLE(msg)                                                                                                \
	do {                                                                                                                       \
		std::cerr << "FATAL: Unreachable code reached: " << (msg) << std::endl;                                                \
		std::abort();                                                                                                          \
	} while (0)
#endif
#else
// Debug build: Assert with message
#define ASSERT_UNREACHABLE(msg) assert(false && (msg))
#endif
