#define DYNLEX_REQUIRE_GNU_SOURCE
#include "platformFeatureTest.h"
#undef DYNLEX_REQUIRE_GNU_SOURCE

#include "processRuntimeInternal.h"

#include "runtimeError.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

typedef struct {
	pid_t process_id;
	int standard_input;
	int standard_output;
	int standard_error;
} DynlexPosixProcess;

typedef struct {
	char **values;
	size_t owned_start;
	size_t count;
} DynlexPosixEnvironment;

static bool environment_name_matches(const char *entry, const DynlexProcessString *name) {
	const char *separator = strchr(entry, '=');
	return separator != NULL && (size_t)(separator - entry) == name->length && memcmp(entry, name->data, name->length) == 0;
}

static bool command_overrides_environment(const DynlexProcessCommand *command, const char *entry) {
	for (size_t index = 0; index < command->environment_count; ++index) {
		if (environment_name_matches(entry, &command->environment[index].name))
			return true;
	}
	return false;
}

static int build_environment(const DynlexProcessCommand *command, DynlexPosixEnvironment *environment) {
	size_t inherited_count = 0;
	if (command->inherit_environment) {
		for (char **entry = environ; *entry != NULL; ++entry) {
			if (!command_overrides_environment(command, *entry))
				inherited_count++;
		}
	}
	size_t override_count = 0;
	for (size_t index = 0; index < command->environment_count; ++index) {
		if (!command->environment[index].unset)
			override_count++;
	}
	if (inherited_count > SIZE_MAX - override_count - 1) {
		dynlex_runtime_set_error("Process environment contains too many entries");
		return -1;
	}
	char **values = calloc(inherited_count + override_count + 1, sizeof(*values));
	if (values == NULL) {
		dynlex_runtime_set_errno_error("Could not allocate process environment", ENOMEM);
		return -1;
	}
	size_t count = 0;
	if (command->inherit_environment) {
		for (char **entry = environ; *entry != NULL; ++entry) {
			if (!command_overrides_environment(command, *entry))
				values[count++] = *entry;
		}
	}
	size_t owned_start = count;
	for (size_t index = 0; index < command->environment_count; ++index) {
		const DynlexProcessEnvironmentEntry *entry = &command->environment[index];
		if (entry->unset)
			continue;
		if (entry->name.length > SIZE_MAX - entry->value.length - 2) {
			dynlex_runtime_set_error("Process environment entry is too large");
			for (size_t owned = owned_start; owned < count; ++owned)
				free(values[owned]);
			free(values);
			return -1;
		}
		size_t length = entry->name.length + entry->value.length + 1;
		char *combined = malloc(length + 1);
		if (combined == NULL) {
			dynlex_runtime_set_errno_error("Could not allocate process environment entry", ENOMEM);
			for (size_t owned = owned_start; owned < count; ++owned)
				free(values[owned]);
			free(values);
			return -1;
		}
		memcpy(combined, entry->name.data, entry->name.length);
		combined[entry->name.length] = '=';
		memcpy(combined + entry->name.length + 1, entry->value.data, entry->value.length);
		combined[length] = '\0';
		values[count++] = combined;
	}
	environment->values = values;
	environment->owned_start = owned_start;
	environment->count = count;
	return 0;
}

static void free_environment(DynlexPosixEnvironment *environment) {
	for (size_t index = environment->owned_start; index < environment->count; ++index)
		free(environment->values[index]);
	free(environment->values);
}

static const char *environment_value(const DynlexPosixEnvironment *environment, const char *name) {
	size_t name_length = strlen(name);
	for (size_t index = 0; index < environment->count; ++index) {
		const char *separator = strchr(environment->values[index], '=');
		if (separator != NULL && (size_t)(separator - environment->values[index]) == name_length &&
			memcmp(environment->values[index], name, name_length) == 0)
			return separator + 1;
	}
	return NULL;
}

static int duplicate_above_standard_descriptors(int descriptor) {
	if (descriptor > STDERR_FILENO)
		return descriptor;
	int replacement = fcntl(descriptor, F_DUPFD_CLOEXEC, STDERR_FILENO + 1);
	if (replacement < 0)
		return -1;
	close(descriptor);
	return replacement;
}

static int create_pipe(int descriptors[2]) {
#if defined(__linux__)
	if (pipe2(descriptors, O_CLOEXEC) != 0)
		return -1;
#else
	if (pipe(descriptors) != 0)
		return -1;
	if (fcntl(descriptors[0], F_SETFD, FD_CLOEXEC) != 0 || fcntl(descriptors[1], F_SETFD, FD_CLOEXEC) != 0) {
		int error_number = errno;
		close(descriptors[0]);
		close(descriptors[1]);
		errno = error_number;
		return -1;
	}
#endif
	for (size_t index = 0; index < 2; ++index) {
		int replacement = duplicate_above_standard_descriptors(descriptors[index]);
		if (replacement < 0) {
			int error_number = errno;
			close(descriptors[0]);
			close(descriptors[1]);
			errno = error_number;
			return -1;
		}
		descriptors[index] = replacement;
	}
	return 0;
}

static void close_pipe(int descriptors[2]) {
	if (descriptors[0] >= 0)
		close(descriptors[0]);
	if (descriptors[1] >= 0)
		close(descriptors[1]);
	descriptors[0] = -1;
	descriptors[1] = -1;
}

static int set_nonblocking(int descriptor) {
	int flags = fcntl(descriptor, F_GETFL);
	if (flags < 0 || fcntl(descriptor, F_SETFL, flags | O_NONBLOCK) != 0)
		return -1;
	return 0;
}

static char **build_arguments(const DynlexProcessCommand *command) {
	if (command->argument_count > SIZE_MAX / sizeof(char *) - 2) {
		dynlex_runtime_set_error("Process argument list contains too many entries");
		return NULL;
	}
	char **arguments = calloc(command->argument_count + 2, sizeof(*arguments));
	if (arguments == NULL) {
		dynlex_runtime_set_errno_error("Could not allocate process argument list", ENOMEM);
		return NULL;
	}
	arguments[0] = command->executable.data;
	for (size_t index = 0; index < command->argument_count; ++index)
		arguments[index + 1] = command->arguments[index].data;
	return arguments;
}

static int add_file_actions(
	posix_spawn_file_actions_t *actions, const DynlexProcessCommand *command, const int input_pipe[2], const int output_pipe[2],
	const int error_pipe[2]
) {
	int result = posix_spawn_file_actions_init(actions);
	if (result != 0)
		return result;
#define ADD_ACTION(call)                                                                                                       \
	do {                                                                                                                       \
		result = (call);                                                                                                       \
		if (result != 0)                                                                                                       \
			goto failure;                                                                                                      \
	} while (0)
	ADD_ACTION(posix_spawn_file_actions_adddup2(actions, input_pipe[0], STDIN_FILENO));
	ADD_ACTION(posix_spawn_file_actions_adddup2(actions, output_pipe[1], STDOUT_FILENO));
	ADD_ACTION(posix_spawn_file_actions_adddup2(actions, error_pipe[1], STDERR_FILENO));
	ADD_ACTION(posix_spawn_file_actions_addclose(actions, input_pipe[0]));
	ADD_ACTION(posix_spawn_file_actions_addclose(actions, input_pipe[1]));
	ADD_ACTION(posix_spawn_file_actions_addclose(actions, output_pipe[0]));
	ADD_ACTION(posix_spawn_file_actions_addclose(actions, output_pipe[1]));
	ADD_ACTION(posix_spawn_file_actions_addclose(actions, error_pipe[0]));
	ADD_ACTION(posix_spawn_file_actions_addclose(actions, error_pipe[1]));
	if (command->has_working_directory)
		ADD_ACTION(posix_spawn_file_actions_addchdir_np(actions, command->working_directory.data));
#undef ADD_ACTION
	return 0;

failure:
#undef ADD_ACTION
	posix_spawn_file_actions_destroy(actions);
	return result;
}

static int spawn_candidate(
	pid_t *process_id, const char *executable, const posix_spawn_file_actions_t *actions, char *const arguments[],
	char *const environment[]
) {
	return posix_spawn(process_id, executable, actions, NULL, arguments, environment);
}

static int spawn_from_path(
	pid_t *process_id, const DynlexProcessCommand *command, const DynlexPosixEnvironment *environment,
	const posix_spawn_file_actions_t *actions, char *const arguments[]
) {
	if (strchr(command->executable.data, '/') != NULL)
		return spawn_candidate(process_id, command->executable.data, actions, arguments, environment->values);

	const char *path = environment_value(environment, "PATH");
	if (path == NULL)
		return ENOENT;
	int access_error = 0;
	const char *entry = path;
	while (true) {
		const char *separator = strchr(entry, ':');
		size_t directory_length = separator == NULL ? strlen(entry) : (size_t)(separator - entry);
		size_t candidate_length =
			directory_length == 0 ? command->executable.length : directory_length + 1 + command->executable.length;
		char *candidate = malloc(candidate_length + 1);
		if (candidate == NULL)
			return ENOMEM;
		size_t offset = 0;
		if (directory_length > 0) {
			memcpy(candidate, entry, directory_length);
			candidate[directory_length] = '/';
			offset = directory_length + 1;
		}
		memcpy(candidate + offset, command->executable.data, command->executable.length + 1);
		int result = spawn_candidate(process_id, candidate, actions, arguments, environment->values);
		free(candidate);
		if (result == 0)
			return 0;
		if (result == EACCES)
			access_error = result;
		else if (result != ENOENT && result != ENOTDIR)
			return result;
		if (separator == NULL)
			break;
		entry = separator + 1;
	}
	return access_error != 0 ? access_error : ENOENT;
}

int dynlex_platform_process_launch(DynlexProcess *process, const DynlexProcessCommand *command) {
	int input_pipe[2] = {-1, -1};
	int output_pipe[2] = {-1, -1};
	int error_pipe[2] = {-1, -1};
	DynlexPosixEnvironment environment = {0};
	char **arguments = NULL;
	posix_spawn_file_actions_t actions;
	bool actions_initialized = false;
	DynlexPosixProcess *platform = NULL;

	if (build_environment(command, &environment) != 0)
		goto failure;
	arguments = build_arguments(command);
	if (arguments == NULL)
		goto failure;
	if (create_pipe(input_pipe) != 0 || create_pipe(output_pipe) != 0 || create_pipe(error_pipe) != 0) {
		dynlex_runtime_set_errno_error("Could not create process pipes", errno);
		goto failure;
	}
	int action_result = add_file_actions(&actions, command, input_pipe, output_pipe, error_pipe);
	if (action_result != 0) {
		dynlex_runtime_set_errno_error("Could not configure process file actions", action_result);
		goto failure;
	}
	actions_initialized = true;
	platform = calloc(1, sizeof(*platform));
	if (platform == NULL) {
		dynlex_runtime_set_errno_error("Could not allocate platform process", ENOMEM);
		goto failure;
	}
	int spawn_result = spawn_from_path(&platform->process_id, command, &environment, &actions, arguments);
	if (spawn_result != 0) {
		dynlex_runtime_set_errno_error("Could not launch process", spawn_result);
		goto failure;
	}

	close(input_pipe[0]);
	input_pipe[0] = -1;
	close(output_pipe[1]);
	output_pipe[1] = -1;
	close(error_pipe[1]);
	error_pipe[1] = -1;
	if (set_nonblocking(input_pipe[1]) != 0 || set_nonblocking(output_pipe[0]) != 0 || set_nonblocking(error_pipe[0]) != 0) {
		int error_number = errno;
		kill(platform->process_id, SIGKILL);
		(void)waitpid(platform->process_id, NULL, 0);
		dynlex_runtime_set_errno_error("Could not configure process pipes", error_number);
		goto failure;
	}
	platform->standard_input = input_pipe[1];
	platform->standard_output = output_pipe[0];
	platform->standard_error = error_pipe[0];
	input_pipe[1] = -1;
	output_pipe[0] = -1;
	error_pipe[0] = -1;
	process->platform = platform;
	posix_spawn_file_actions_destroy(&actions);
	free(arguments);
	free_environment(&environment);
	return 0;

failure:
	if (actions_initialized)
		posix_spawn_file_actions_destroy(&actions);
	close_pipe(input_pipe);
	close_pipe(output_pipe);
	close_pipe(error_pipe);
	free(platform);
	free(arguments);
	free_environment(&environment);
	return -1;
}

static int read_stream_chunk(
	DynlexProcess *process, DynlexProcessStream stream, int *descriptor, size_t maximum_length, size_t *read_count
) {
	*read_count = 0;
	if (maximum_length == 0)
		return 0;
	char chunk[4096];
	while (*descriptor >= 0) {
		size_t requested = maximum_length < sizeof(chunk) ? maximum_length : sizeof(chunk);
		ssize_t count = read(*descriptor, chunk, requested);
		if (count > 0) {
			if (dynlex_process_append_output(process, stream, chunk, (size_t)count) != 0)
				return -1;
			*read_count = (size_t)count;
			return 0;
		}
		if (count == 0) {
			close(*descriptor);
			*descriptor = -1;
			dynlex_process_mark_stream_closed(process, stream);
			return 0;
		}
		if (errno == EINTR)
			continue;
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			return 0;
		dynlex_runtime_set_errno_error("Could not read process output", errno);
		return -1;
	}
	return 0;
}

static int drain_buffered_stream_after_exit(DynlexProcess *process, DynlexProcessStream stream, int *descriptor) {
	if (*descriptor < 0)
		return 0;
	int available = 0;
	int result;
	do {
		result = ioctl(*descriptor, FIONREAD, &available);
	} while (result != 0 && errno == EINTR);
	if (result != 0) {
		dynlex_runtime_set_errno_error("Could not inspect buffered process output", errno);
		return -1;
	}
	if (available < 0)
		abort();
	size_t remaining = (size_t)available;
	while (remaining > 0) {
		size_t read_count = 0;
		if (read_stream_chunk(process, stream, descriptor, remaining, &read_count) != 0)
			return -1;
		if (read_count == 0)
			break;
		remaining -= read_count;
	}
	return 0;
}

static int record_wait_status(DynlexProcess *process, int status) {
	if (WIFEXITED(status))
		dynlex_process_mark_finished(process, WEXITSTATUS(status), 0);
	else if (WIFSIGNALED(status)) {
		int32_t signal_number = WTERMSIG(status);
		dynlex_process_mark_finished(process, 128 + signal_number, signal_number);
	} else {
		dynlex_runtime_set_error("Process wait returned an unsupported status");
		return -1;
	}
	return 0;
}

static int update_process_status(DynlexProcess *process, bool wait) {
	if (process->finished)
		return 0;
	DynlexPosixProcess *platform = process->platform;
	int status = 0;
	pid_t result;
	do {
		result = waitpid(platform->process_id, &status, wait ? 0 : WNOHANG);
	} while (result < 0 && errno == EINTR);
	if (result < 0) {
		dynlex_runtime_set_errno_error("Could not wait for process", errno);
		return -1;
	}
	if (result == 0)
		return 0;
	return record_wait_status(process, status);
}

static bool requested_stream_ready(DynlexProcess *process, DynlexProcessStream stream) {
	if (stream == 0)
		return false;
	if (stream == DYNLEX_PROCESS_STREAM_ANY)
		return process->standard_output.length > 0 || process->standard_error.length > 0 ||
			   (process->standard_output_closed && process->standard_error_closed);
	DynlexProcessBuffer *buffer = stream == DYNLEX_PROCESS_STREAM_STDOUT ? &process->standard_output : &process->standard_error;
	bool closed = stream == DYNLEX_PROCESS_STREAM_STDOUT ? process->standard_output_closed : process->standard_error_closed;
	return buffer->length > 0 || closed;
}

static int close_output_after_exit(DynlexProcess *process, DynlexPosixProcess *platform) {
	int result = drain_buffered_stream_after_exit(process, DYNLEX_PROCESS_STREAM_STDOUT, &platform->standard_output);
	if (result == 0)
		result = drain_buffered_stream_after_exit(process, DYNLEX_PROCESS_STREAM_STDERR, &platform->standard_error);
	if (platform->standard_output >= 0) {
		close(platform->standard_output);
		platform->standard_output = -1;
		dynlex_process_mark_stream_closed(process, DYNLEX_PROCESS_STREAM_STDOUT);
	}
	if (platform->standard_error >= 0) {
		close(platform->standard_error);
		platform->standard_error = -1;
		dynlex_process_mark_stream_closed(process, DYNLEX_PROCESS_STREAM_STDERR);
	}
	return result;
}

static int64_t monotonic_milliseconds(void) {
	struct timespec current;
	if (clock_gettime(CLOCK_MONOTONIC, &current) != 0)
		return -1;
	return (int64_t)current.tv_sec * 1000 + current.tv_nsec / 1000000;
}

int dynlex_platform_process_pump(
	DynlexProcess *process, int64_t timeout_milliseconds, DynlexProcessStream requested_stream
) {
	DynlexPosixProcess *platform = process->platform;
	int64_t started = 0;
	if (timeout_milliseconds > 0) {
		started = monotonic_milliseconds();
		if (started < 0) {
			dynlex_runtime_set_errno_error("Could not read the monotonic clock", errno);
			return -1;
		}
	}
	while (true) {
		size_t read_output = 0;
		size_t read_error = 0;
		if (read_stream_chunk(process, DYNLEX_PROCESS_STREAM_STDOUT, &platform->standard_output, SIZE_MAX, &read_output) != 0 ||
			read_stream_chunk(process, DYNLEX_PROCESS_STREAM_STDERR, &platform->standard_error, SIZE_MAX, &read_error) != 0 ||
			update_process_status(process, false) != 0)
			return -1;
		if (process->finished)
			return close_output_after_exit(process, platform);
		if (requested_stream_ready(process, requested_stream))
			return 0;
		if (timeout_milliseconds == 0)
			return 0;

		int poll_timeout = -1;
		if (timeout_milliseconds > 0) {
			int64_t current = monotonic_milliseconds();
			if (current < 0) {
				dynlex_runtime_set_errno_error("Could not read the monotonic clock", errno);
				return -1;
			}
			int64_t remaining = timeout_milliseconds - (current - started);
			if (remaining <= 0)
				return 0;
			poll_timeout = remaining > INT_MAX ? INT_MAX : (int)remaining;
		}

		struct pollfd descriptors[2];
		nfds_t count = 0;
		if (platform->standard_output >= 0)
			descriptors[count++] = (struct pollfd){platform->standard_output, POLLIN | POLLHUP, 0};
		if (platform->standard_error >= 0)
			descriptors[count++] = (struct pollfd){platform->standard_error, POLLIN | POLLHUP, 0};
		if (count == 0) {
			if (timeout_milliseconds < 0)
				return update_process_status(process, true);
			int wait_result;
			do {
				wait_result = poll(NULL, 0, poll_timeout);
			} while (wait_result < 0 && errno == EINTR);
			if (wait_result < 0) {
				dynlex_runtime_set_errno_error("Could not wait for process", errno);
				return -1;
			}
			return update_process_status(process, false);
		}
		int poll_result;
		do {
			poll_result = poll(descriptors, count, poll_timeout);
		} while (poll_result < 0 && errno == EINTR);
		if (poll_result < 0) {
			dynlex_runtime_set_errno_error("Could not monitor process pipes", errno);
			return -1;
		}
		if (poll_result == 0) {
			if (update_process_status(process, false) != 0)
				return -1;
			return process->finished ? close_output_after_exit(process, platform) : 0;
		}
	}
}

static ssize_t write_without_sigpipe(int descriptor, const char *data, size_t length) {
	sigset_t blocked;
	sigset_t previous;
	sigset_t pending;
	sigemptyset(&blocked);
	sigaddset(&blocked, SIGPIPE);
	int mask_result = pthread_sigmask(SIG_BLOCK, &blocked, &previous);
	if (mask_result != 0) {
		errno = mask_result;
		return -1;
	}
	bool was_pending = sigpending(&pending) == 0 && sigismember(&pending, SIGPIPE);
	ssize_t result = write(descriptor, data, length);
	int error_number = errno;
	if (result < 0 && error_number == EPIPE && !was_pending) {
		struct timespec timeout = {0, 0};
		while (sigtimedwait(&blocked, NULL, &timeout) < 0 && errno == EINTR) {
		}
	}
	(void)pthread_sigmask(SIG_SETMASK, &previous, NULL);
	errno = error_number;
	return result;
}

int dynlex_platform_process_write(DynlexProcess *process, const char *data, size_t length, size_t *written) {
	DynlexPosixProcess *platform = process->platform;
	*written = 0;
	if (length == 0)
		return 0;
	if (platform->standard_input < 0) {
		dynlex_runtime_set_error("Process standard input is closed");
		return -1;
	}
	while (*written < length) {
		ssize_t count = write_without_sigpipe(platform->standard_input, data + *written, length - *written);
		if (count > 0) {
			*written += (size_t)count;
			continue;
		}
		if (count < 0 && errno == EINTR)
			continue;
		if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
			int error_number = errno;
			close(platform->standard_input);
			platform->standard_input = -1;
			dynlex_runtime_set_errno_error("Could not write process standard input", error_number);
			return -1;
		}
		if (dynlex_platform_process_pump(process, 0, 0) != 0)
			return -1;
		struct pollfd descriptors[3];
		nfds_t descriptor_count = 0;
		descriptors[descriptor_count++] = (struct pollfd){platform->standard_input, POLLOUT | POLLHUP, 0};
		if (platform->standard_output >= 0)
			descriptors[descriptor_count++] = (struct pollfd){platform->standard_output, POLLIN | POLLHUP, 0};
		if (platform->standard_error >= 0)
			descriptors[descriptor_count++] = (struct pollfd){platform->standard_error, POLLIN | POLLHUP, 0};
		int poll_result;
		do {
			poll_result = poll(descriptors, descriptor_count, -1);
		} while (poll_result < 0 && errno == EINTR);
		if (poll_result < 0) {
			dynlex_runtime_set_errno_error("Could not monitor process standard input", errno);
			return -1;
		}
		if (dynlex_platform_process_pump(process, 0, 0) != 0)
			return -1;
	}
	return 0;
}

int dynlex_platform_process_close_input(DynlexProcess *process) {
	DynlexPosixProcess *platform = process->platform;
	if (platform->standard_input >= 0) {
		if (close(platform->standard_input) != 0) {
			dynlex_runtime_set_errno_error("Could not close process standard input", errno);
			platform->standard_input = -1;
			return -1;
		}
		platform->standard_input = -1;
	}
	return 0;
}

int dynlex_platform_process_terminate(DynlexProcess *process) {
	DynlexPosixProcess *platform = process->platform;
	if (update_process_status(process, false) != 0)
		return -1;
	if (process->finished)
		return 0;
	if (kill(platform->process_id, SIGTERM) == 0) {
		process->termination_requested = true;
		return update_process_status(process, false);
	}
	int error_number = errno;
	if (error_number == ESRCH && update_process_status(process, false) == 0 && process->finished)
		return 0;
	dynlex_runtime_set_errno_error("Could not terminate process", error_number);
	return -1;
}

int dynlex_platform_process_kill(DynlexProcess *process) {
	DynlexPosixProcess *platform = process->platform;
	if (update_process_status(process, false) != 0)
		return -1;
	if (process->finished)
		return 0;
	if (kill(platform->process_id, SIGKILL) == 0) {
		process->termination_requested = true;
		return update_process_status(process, false);
	}
	int error_number = errno;
	if (error_number == ESRCH && update_process_status(process, false) == 0 && process->finished)
		return 0;
	dynlex_runtime_set_errno_error("Could not kill process", error_number);
	return -1;
}

static void close_discarded_output(DynlexProcess *process, DynlexPosixProcess *platform) {
	if (platform->standard_output >= 0) {
		close(platform->standard_output);
		platform->standard_output = -1;
	}
	if (platform->standard_error >= 0) {
		close(platform->standard_error);
		platform->standard_error = -1;
	}
	dynlex_process_mark_stream_closed(process, DYNLEX_PROCESS_STREAM_STDOUT);
	dynlex_process_mark_stream_closed(process, DYNLEX_PROCESS_STREAM_STDERR);
}

int dynlex_platform_process_cleanup(DynlexProcess *process) {
	DynlexPosixProcess *platform = process->platform;
	if (platform->standard_input >= 0) {
		close(platform->standard_input);
		platform->standard_input = -1;
	}
	if (!process->finished) {
		if (kill(platform->process_id, SIGKILL) == 0)
			process->termination_requested = true;
		else if (errno != ESRCH) {
			dynlex_runtime_set_errno_error("Could not kill process during cleanup", errno);
			return -1;
		}
		close_discarded_output(process, platform);
		int status = 0;
		pid_t result;
		do {
			result = waitpid(platform->process_id, &status, 0);
		} while (result < 0 && errno == EINTR);
		if (result < 0) {
			dynlex_runtime_set_errno_error("Could not reap process during cleanup", errno);
			return -1;
		}
		if (record_wait_status(process, status) != 0)
			return -1;
	} else
		close_discarded_output(process, platform);
	process->cleanup_ready = true;
	return 0;
}

void dynlex_platform_process_destroy(DynlexProcess *process) {
	DynlexPosixProcess *platform = process->platform;
	if (platform == NULL)
		return;
	if (!process->cleanup_ready)
		abort();
	if (platform->standard_input >= 0)
		close(platform->standard_input);
	if (platform->standard_output >= 0)
		close(platform->standard_output);
	if (platform->standard_error >= 0)
		close(platform->standard_error);
	free(platform);
}

void dynlex_platform_process_lock(DynlexProcess *process) { (void)process; }

void dynlex_platform_process_unlock(DynlexProcess *process) { (void)process; }
