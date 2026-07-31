#define _POSIX_C_SOURCE 200809L

#include "processRuntimePosixWrite.h"

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define CHECK(condition)                                                                                                       \
	do {                                                                                                                       \
		if (!(condition)) {                                                                                                    \
			fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, __LINE__, #condition);                                    \
			return 1;                                                                                                          \
		}                                                                                                                      \
	} while (0)

static int broken_pipe(void) {
	int descriptors[2];
	if (pipe(descriptors) != 0)
		return -1;
	if (close(descriptors[0]) != 0) {
		close(descriptors[1]);
		return -1;
	}
	return descriptors[1];
}

static int expect_broken_pipe_error(void) {
	int descriptor = broken_pipe();
	CHECK(descriptor >= 0);
	errno = 0;
	ssize_t result = dynlex_posix_write_without_sigpipe(descriptor, "x", 1);
	int error_number = errno;
	CHECK(close(descriptor) == 0);
	CHECK(result == -1);
	CHECK(error_number == EPIPE);
	return 0;
}

static int signal_is_pending(int signal_number) {
	sigset_t pending;
	if (sigpending(&pending) != 0)
		return -1;
	return sigismember(&pending, signal_number);
}

int main(void) {
	struct sigaction original_action;
	CHECK(sigaction(SIGPIPE, NULL, &original_action) == 0);
	struct sigaction default_action;
	memset(&default_action, 0, sizeof(default_action));
	default_action.sa_handler = SIG_DFL;
	CHECK(sigemptyset(&default_action.sa_mask) == 0);
	CHECK(sigaction(SIGPIPE, &default_action, NULL) == 0);

	sigset_t original_mask;
	CHECK(pthread_sigmask(SIG_SETMASK, NULL, &original_mask) == 0);
	sigset_t test_mask = original_mask;
	CHECK(sigdelset(&test_mask, SIGPIPE) == 0);
	CHECK(sigaddset(&test_mask, SIGUSR1) == 0);
	CHECK(pthread_sigmask(SIG_SETMASK, &test_mask, NULL) == 0);
	CHECK(expect_broken_pipe_error() == 0);
	sigset_t restored_mask;
	CHECK(pthread_sigmask(SIG_SETMASK, NULL, &restored_mask) == 0);
	CHECK(sigismember(&restored_mask, SIGPIPE) == 0);
	CHECK(sigismember(&restored_mask, SIGUSR1) == 1);

	struct sigaction ignored_action = default_action;
	ignored_action.sa_handler = SIG_IGN;
	CHECK(sigaction(SIGPIPE, &ignored_action, NULL) == 0);
	CHECK(expect_broken_pipe_error() == 0);
	CHECK(signal_is_pending(SIGPIPE) == 0);

	CHECK(sigaction(SIGPIPE, &default_action, NULL) == 0);
	sigset_t blocked_mask = test_mask;
	CHECK(sigaddset(&blocked_mask, SIGPIPE) == 0);
	CHECK(pthread_sigmask(SIG_SETMASK, &blocked_mask, NULL) == 0);
	CHECK(pthread_kill(pthread_self(), SIGPIPE) == 0);
	CHECK(signal_is_pending(SIGPIPE) == 1);
	CHECK(expect_broken_pipe_error() == 0);
	CHECK(signal_is_pending(SIGPIPE) == 1);
	CHECK(pthread_sigmask(SIG_SETMASK, NULL, &restored_mask) == 0);
	CHECK(sigismember(&restored_mask, SIGPIPE) == 1);
	CHECK(sigismember(&restored_mask, SIGUSR1) == 1);
	sigset_t sigpipe_set;
	CHECK(sigemptyset(&sigpipe_set) == 0);
	CHECK(sigaddset(&sigpipe_set, SIGPIPE) == 0);
	int received_signal = 0;
	CHECK(sigwait(&sigpipe_set, &received_signal) == 0);
	CHECK(received_signal == SIGPIPE);
	CHECK(signal_is_pending(SIGPIPE) == 0);

	CHECK(pthread_sigmask(SIG_SETMASK, &original_mask, NULL) == 0);
	CHECK(sigaction(SIGPIPE, &original_action, NULL) == 0);
	return 0;
}
