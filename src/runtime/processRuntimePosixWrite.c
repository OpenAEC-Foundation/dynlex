#include "platformFeatureTest.h"

#include "processRuntimePosixWrite.h"

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>

static int signal_pending(int signal_number, bool *pending) {
	sigset_t signals;
	if (sigpending(&signals) != 0)
		return errno;
	int membership = sigismember(&signals, signal_number);
	if (membership < 0)
		return errno;
	*pending = membership == 1;
	return 0;
}

ssize_t dynlex_posix_write_without_sigpipe(int descriptor, const char *data, size_t length) {
	sigset_t blocked;
	sigset_t previous;
	sigemptyset(&blocked);
	sigaddset(&blocked, SIGPIPE);
	int mask_result = pthread_sigmask(SIG_BLOCK, &blocked, &previous);
	if (mask_result != 0) {
		errno = mask_result;
		return -1;
	}
	bool was_pending = false;
	int signal_result = signal_pending(SIGPIPE, &was_pending);
	if (signal_result != 0) {
		int restore_result = pthread_sigmask(SIG_SETMASK, &previous, NULL);
		errno = restore_result != 0 ? restore_result : signal_result;
		return -1;
	}
	ssize_t result = write(descriptor, data, length);
	int error_number = errno;
	if (result < 0 && error_number == EPIPE && !was_pending) {
		bool is_pending = false;
		signal_result = signal_pending(SIGPIPE, &is_pending);
		if (signal_result == 0 && is_pending) {
			int received_signal = 0;
			signal_result = sigwait(&blocked, &received_signal);
			if (signal_result == 0 && received_signal != SIGPIPE)
				abort();
		}
	}
	int restore_result = pthread_sigmask(SIG_SETMASK, &previous, NULL);
	if (signal_result != 0 || restore_result != 0) {
		errno = signal_result != 0 ? signal_result : restore_result;
		return -1;
	}
	errno = error_number;
	return result;
}
