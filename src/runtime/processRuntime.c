#include "processRuntimeInternal.h"

#include "runtimeError.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static bool checked_add(size_t left, size_t right, size_t *result) {
	if (left > SIZE_MAX - right)
		return false;
	*result = left + right;
	return true;
}

static const size_t maximum_process_output_length = INT32_MAX - sizeof(int32_t) - 1;

static char *copy_text(const char *data, size_t length) {
	size_t allocation_size = 0;
	if (!checked_add(length, 1, &allocation_size)) {
		dynlex_runtime_set_error("Process text is too large");
		return NULL;
	}
	char *copy = malloc(allocation_size);
	if (copy == NULL) {
		dynlex_runtime_set_errno_error("Could not allocate process text", ENOMEM);
		return NULL;
	}
	if (length > 0)
		memcpy(copy, data, length);
	copy[length] = '\0';
	return copy;
}

static bool text_is_valid(const char *data, size_t length) { return data != NULL && memchr(data, '\0', length) == NULL; }

static void set_configuration_error(DynlexProcessCommand *command) {
	if (command->configuration_error[0] != '\0')
		return;
	size_t length = dynlex_runtime_error_message(command->configuration_error, sizeof(command->configuration_error));
	if (length == 0) {
		const char *fallback = "Invalid process command";
		memcpy(command->configuration_error, fallback, strlen(fallback) + 1);
	}
}

static int replace_text(DynlexProcessCommand *command, DynlexProcessString *destination, const char *data, size_t length) {
	if (!text_is_valid(data, length)) {
		dynlex_runtime_set_error("Process text must not be null or contain a zero byte");
		set_configuration_error(command);
		return -1;
	}
	char *copy = copy_text(data, length);
	if (copy == NULL) {
		set_configuration_error(command);
		return -1;
	}
	free(destination->data);
	destination->data = copy;
	destination->length = length;
	return 0;
}

static int grow_array(void **array, size_t *capacity, size_t required, size_t element_size) {
	if (required <= *capacity)
		return 0;
	size_t next_capacity = *capacity == 0 ? 4 : *capacity;
	while (next_capacity < required) {
		if (next_capacity > SIZE_MAX / 2) {
			dynlex_runtime_set_error("Process command contains too many entries");
			return -1;
		}
		next_capacity *= 2;
	}
	if (next_capacity > SIZE_MAX / element_size) {
		dynlex_runtime_set_error("Process command allocation is too large");
		return -1;
	}
	void *resized = realloc(*array, next_capacity * element_size);
	if (resized == NULL) {
		dynlex_runtime_set_errno_error("Could not grow process command", ENOMEM);
		return -1;
	}
	*array = resized;
	*capacity = next_capacity;
	return 0;
}

void *dynlex_process_command_create(const char *executable, size_t executable_length) {
	dynlex_runtime_clear_error();
	DynlexProcessCommand *command = calloc(1, sizeof(*command));
	if (command == NULL) {
		dynlex_runtime_set_errno_error("Could not allocate process command", ENOMEM);
		return NULL;
	}
	command->inherit_environment = true;
	if (replace_text(command, &command->executable, executable, executable_length) != 0 || command->executable.length == 0) {
		if (command->configuration_error[0] == '\0') {
			dynlex_runtime_set_error("Process executable must not be empty");
			set_configuration_error(command);
		}
	}
	return command;
}

void dynlex_process_command_retain(DynlexProcessCommand *command) {
	if (command != NULL) {
		if (command->references == SIZE_MAX)
			abort();
		command->references++;
	}
}

void dynlex_process_command_release(DynlexProcessCommand *command) {
	if (command == NULL)
		return;
	if (command->references == 0)
		abort();
	if (--command->references != 0)
		return;
	free(command->executable.data);
	free(command->working_directory.data);
	for (size_t index = 0; index < command->argument_count; ++index)
		free(command->arguments[index].data);
	free(command->arguments);
	for (size_t index = 0; index < command->environment_count; ++index) {
		free(command->environment[index].name.data);
		free(command->environment[index].value.data);
	}
	free(command->environment);
	free(command);
}

int dynlex_process_command_add_argument(DynlexProcessCommand *command, const char *argument, size_t argument_length) {
	dynlex_runtime_clear_error();
	if (command == NULL) {
		dynlex_runtime_set_error("Process command is null");
		return -1;
	}
	if (!text_is_valid(argument, argument_length)) {
		dynlex_runtime_set_error("Process argument must not be null or contain a zero byte");
		set_configuration_error(command);
		return -1;
	}
	if (grow_array(
			(void **)&command->arguments, &command->argument_capacity, command->argument_count + 1, sizeof(*command->arguments)
		) != 0) {
		set_configuration_error(command);
		return -1;
	}
	char *copy = copy_text(argument, argument_length);
	if (copy == NULL) {
		set_configuration_error(command);
		return -1;
	}
	command->arguments[command->argument_count++] = (DynlexProcessString){copy, argument_length};
	return 0;
}

int dynlex_process_command_set_working_directory(
	DynlexProcessCommand *command, const char *directory, size_t directory_length
) {
	dynlex_runtime_clear_error();
	if (command == NULL) {
		dynlex_runtime_set_error("Process command is null");
		return -1;
	}
	if (replace_text(command, &command->working_directory, directory, directory_length) != 0)
		return -1;
	command->has_working_directory = true;
	return 0;
}

void dynlex_process_command_inherit_environment(DynlexProcessCommand *command, int32_t inherit_environment) {
	if (command != NULL)
		command->inherit_environment = inherit_environment != 0;
}

static bool valid_environment_name(const char *name, size_t name_length) {
	return text_is_valid(name, name_length) && name_length > 0 && memchr(name, '=', name_length) == NULL;
}

static size_t find_environment_entry(DynlexProcessCommand *command, const char *name, size_t name_length) {
	for (size_t index = 0; index < command->environment_count; ++index) {
		const DynlexProcessString *candidate = &command->environment[index].name;
		if (candidate->length == name_length && memcmp(candidate->data, name, name_length) == 0)
			return index;
	}
	return SIZE_MAX;
}

static DynlexProcessEnvironmentEntry *environment_entry(DynlexProcessCommand *command, const char *name, size_t name_length) {
	size_t existing = find_environment_entry(command, name, name_length);
	if (existing != SIZE_MAX) {
		DynlexProcessEnvironmentEntry entry = command->environment[existing];
		size_t following = command->environment_count - existing - 1;
		if (following > 0)
			memmove(
				&command->environment[existing], &command->environment[existing + 1], following * sizeof(*command->environment)
			);
		command->environment[command->environment_count - 1] = entry;
		return &command->environment[command->environment_count - 1];
	}
	if (grow_array(
			(void **)&command->environment, &command->environment_capacity, command->environment_count + 1,
			sizeof(*command->environment)
		) != 0) {
		set_configuration_error(command);
		return NULL;
	}
	char *name_copy = copy_text(name, name_length);
	if (name_copy == NULL) {
		set_configuration_error(command);
		return NULL;
	}
	DynlexProcessEnvironmentEntry *entry = &command->environment[command->environment_count++];
	*entry = (DynlexProcessEnvironmentEntry){
		.name = {name_copy, name_length},
	};
	return entry;
}

int dynlex_process_command_set_environment(
	DynlexProcessCommand *command, const char *name, size_t name_length, const char *value, size_t value_length
) {
	dynlex_runtime_clear_error();
	if (command == NULL) {
		dynlex_runtime_set_error("Process command is null");
		return -1;
	}
	if (!valid_environment_name(name, name_length)) {
		dynlex_runtime_set_error("Environment variable name must be nonempty and contain neither '=' nor zero bytes");
		set_configuration_error(command);
		return -1;
	}
	if (!text_is_valid(value, value_length)) {
		dynlex_runtime_set_error("Environment variable value must not be null or contain a zero byte");
		set_configuration_error(command);
		return -1;
	}
	DynlexProcessEnvironmentEntry *entry = environment_entry(command, name, name_length);
	if (entry == NULL)
		return -1;
	char *value_copy = copy_text(value, value_length);
	if (value_copy == NULL) {
		set_configuration_error(command);
		return -1;
	}
	free(entry->value.data);
	entry->value = (DynlexProcessString){value_copy, value_length};
	entry->unset = false;
	return 0;
}

int dynlex_process_command_unset_environment(DynlexProcessCommand *command, const char *name, size_t name_length) {
	dynlex_runtime_clear_error();
	if (command == NULL) {
		dynlex_runtime_set_error("Process command is null");
		return -1;
	}
	if (!valid_environment_name(name, name_length)) {
		dynlex_runtime_set_error("Environment variable name must be nonempty and contain neither '=' nor zero bytes");
		set_configuration_error(command);
		return -1;
	}
	DynlexProcessEnvironmentEntry *entry = environment_entry(command, name, name_length);
	if (entry == NULL)
		return -1;
	free(entry->value.data);
	entry->value = (DynlexProcessString){0};
	entry->unset = true;
	return 0;
}

static DynlexProcessBuffer *output_buffer(DynlexProcess *process, DynlexProcessStream stream) {
	return stream == DYNLEX_PROCESS_STREAM_STDOUT ? &process->standard_output : &process->standard_error;
}

static bool stream_is_closed(const DynlexProcess *process, DynlexProcessStream stream) {
	return stream == DYNLEX_PROCESS_STREAM_STDOUT ? process->standard_output_closed : process->standard_error_closed;
}

int dynlex_process_append_output(DynlexProcess *process, DynlexProcessStream stream, const char *data, size_t length) {
	if (length == 0)
		return 0;
	DynlexProcessBuffer *buffer = output_buffer(process, stream);
	if (buffer->offset > buffer->capacity || buffer->length > buffer->capacity - buffer->offset)
		abort();
	size_t available_suffix = buffer->capacity - buffer->offset - buffer->length;
	if (buffer->offset > 0 && available_suffix < length) {
		memmove(buffer->data, buffer->data + buffer->offset, buffer->length);
		buffer->offset = 0;
	}
	size_t required = 0;
	if (!checked_add(buffer->offset, buffer->length, &required) || !checked_add(required, length, &required)) {
		dynlex_runtime_set_error("Captured process output is too large");
		return -1;
	}
	if (length > maximum_process_output_length || buffer->length > maximum_process_output_length - length) {
		dynlex_runtime_set_error("Captured process output exceeds the maximum DynLex string length");
		return -1;
	}
	if (required > buffer->capacity) {
		size_t next_capacity = buffer->capacity == 0 ? 4096 : buffer->capacity;
		while (next_capacity < required) {
			if (next_capacity > maximum_process_output_length / 2) {
				next_capacity = maximum_process_output_length;
				break;
			}
			next_capacity *= 2;
		}
		if (next_capacity < required)
			abort();
		char *resized = realloc(buffer->data, next_capacity);
		if (resized == NULL) {
			dynlex_runtime_set_errno_error("Could not grow captured process output", ENOMEM);
			return -1;
		}
		buffer->data = resized;
		buffer->capacity = next_capacity;
	}
	memcpy(buffer->data + buffer->offset + buffer->length, data, length);
	buffer->length += length;
	return 0;
}

void dynlex_process_mark_stream_closed(DynlexProcess *process, DynlexProcessStream stream) {
	if (stream == DYNLEX_PROCESS_STREAM_STDOUT)
		process->standard_output_closed = true;
	else
		process->standard_error_closed = true;
}

void dynlex_process_mark_finished(DynlexProcess *process, int64_t exit_code, int32_t termination_signal) {
	process->finished = true;
	process->exit_code = exit_code;
	process->termination_signal = termination_signal;
}

static void capture_runtime_error(char *destination, size_t capacity, const char *fallback) {
	size_t error_length = dynlex_runtime_error_message(destination, capacity);
	if (error_length == 0)
		memcpy(destination, fallback, strlen(fallback) + 1);
}

void *dynlex_process_launch(DynlexProcessCommand *command) {
	dynlex_runtime_clear_error();
	DynlexProcess *process = calloc(1, sizeof(*process));
	if (process == NULL) {
		dynlex_runtime_set_errno_error("Could not allocate process", ENOMEM);
		return NULL;
	}
	if (command == NULL) {
		dynlex_runtime_set_error("Process command is null");
		capture_runtime_error(process->launch_error, sizeof(process->launch_error), "Process command is null");
		return process;
	}
	if (command->configuration_error[0] != '\0') {
		dynlex_runtime_set_error(command->configuration_error);
		capture_runtime_error(process->launch_error, sizeof(process->launch_error), "Invalid process command");
		return process;
	}
	if (dynlex_platform_process_launch(process, command) != 0) {
		capture_runtime_error(process->launch_error, sizeof(process->launch_error), "Could not launch process");
		return process;
	}
	process->launched = true;
	return process;
}

void dynlex_process_retain(DynlexProcess *process) {
	if (process != NULL) {
		if (process->references == SIZE_MAX)
			abort();
		process->references++;
	}
}

static int ensure_usable_process(DynlexProcess *process) {
	if (process == NULL) {
		dynlex_runtime_set_error("Process was not launched");
		return -1;
	}
	if (!process->launched) {
		dynlex_runtime_set_error(process->launch_error);
		return -1;
	}
	if (process->cleaned) {
		dynlex_runtime_set_error("Process has already been cleaned up");
		return -1;
	}
	return 0;
}

int dynlex_process_write(DynlexProcess *process, const char *data, size_t length, size_t *written) {
	dynlex_runtime_clear_error();
	if (ensure_usable_process(process) != 0)
		return -1;
	if ((data == NULL && length != 0) || written == NULL) {
		dynlex_runtime_set_error("Invalid process write arguments");
		return -1;
	}
	return dynlex_platform_process_write(process, data, length, written);
}

int dynlex_process_close_input(DynlexProcess *process) {
	dynlex_runtime_clear_error();
	if (ensure_usable_process(process) != 0)
		return -1;
	return dynlex_platform_process_close_input(process);
}

int dynlex_process_read(
	DynlexProcess *process, int32_t stream_value, int32_t wait, char *buffer, size_t capacity, size_t *read_count,
	int32_t *end_of_stream
) {
	dynlex_runtime_clear_error();
	if (ensure_usable_process(process) != 0)
		return -1;
	if ((stream_value != DYNLEX_PROCESS_STREAM_STDOUT && stream_value != DYNLEX_PROCESS_STREAM_STDERR) ||
		(buffer == NULL && capacity != 0) || read_count == NULL || end_of_stream == NULL) {
		dynlex_runtime_set_error("Invalid process read arguments");
		return -1;
	}
	DynlexProcessStream stream = (DynlexProcessStream)stream_value;
	dynlex_platform_process_lock(process);
	DynlexProcessBuffer *captured = output_buffer(process, stream);
	bool needs_pump = captured->length == 0 && !stream_is_closed(process, stream);
	dynlex_platform_process_unlock(process);
	if (needs_pump && dynlex_platform_process_pump(process, wait != 0, stream) != 0)
		return -1;

	dynlex_platform_process_lock(process);
	captured = output_buffer(process, stream);
	size_t copied = captured->length < capacity ? captured->length : capacity;
	if (copied > 0) {
		memcpy(buffer, captured->data + captured->offset, copied);
		captured->offset += copied;
		captured->length -= copied;
		if (captured->length == 0)
			captured->offset = 0;
	}
	*read_count = copied;
	*end_of_stream = stream_is_closed(process, stream) && captured->length == 0 ? 1 : 0;
	dynlex_platform_process_unlock(process);
	return 0;
}

static int process_status(
	DynlexProcess *process, bool wait, int32_t *finished, int64_t *exit_code, int32_t *terminated, int32_t *termination_signal
) {
	dynlex_runtime_clear_error();
	if (ensure_usable_process(process) != 0)
		return -1;
	if (finished == NULL || exit_code == NULL || terminated == NULL || termination_signal == NULL) {
		dynlex_runtime_set_error("Invalid process status arguments");
		return -1;
	}
	if ((!process->finished || wait) && dynlex_platform_process_pump(process, wait, 0) != 0)
		return -1;
	*finished = process->finished ? 1 : 0;
	*exit_code = process->exit_code;
	*termination_signal = process->termination_signal;
	*terminated = process->finished && (process->termination_requested || process->termination_signal != 0) ? 1 : 0;
	return 0;
}

int dynlex_process_poll(
	DynlexProcess *process, int32_t *finished, int64_t *exit_code, int32_t *terminated, int32_t *termination_signal
) {
	return process_status(process, false, finished, exit_code, terminated, termination_signal);
}

int dynlex_process_wait(
	DynlexProcess *process, int32_t *finished, int64_t *exit_code, int32_t *terminated, int32_t *termination_signal
) {
	return process_status(process, true, finished, exit_code, terminated, termination_signal);
}

int dynlex_process_terminate(DynlexProcess *process) {
	dynlex_runtime_clear_error();
	if (ensure_usable_process(process) != 0)
		return -1;
	if (process->finished)
		return 0;
	if (dynlex_platform_process_terminate(process) != 0)
		return -1;
	if (!process->finished)
		process->termination_requested = true;
	return 0;
}

int dynlex_process_cleanup(DynlexProcess *process) {
	dynlex_runtime_clear_error();
	if (process == NULL) {
		dynlex_runtime_set_error("Process was not launched");
		return -1;
	}
	if (process->cleaned)
		return 0;
	if (!process->launched) {
		process->cleaned = true;
		return 0;
	}
	int result = dynlex_platform_process_cleanup(process);
	if (process->cleanup_ready) {
		dynlex_platform_process_destroy(process);
		process->platform = NULL;
		process->cleaned = true;
	}
	return result;
}

void dynlex_process_release(DynlexProcess *process) {
	if (process == NULL)
		return;
	if (process->references == 0)
		abort();
	if (--process->references != 0)
		return;
	(void)dynlex_process_cleanup(process);
	if (process->platform != NULL || !process->cleaned)
		abort();
	free(process->standard_output.data);
	free(process->standard_error.data);
	free(process);
}

int dynlex_process_communicate(DynlexProcess *process, const char *input, size_t input_length) {
	dynlex_runtime_clear_error();
	if (ensure_usable_process(process) != 0)
		return -1;
	char first_error[512] = {0};
	size_t written = 0;
	if (dynlex_process_write(process, input, input_length, &written) != 0)
		capture_runtime_error(first_error, sizeof(first_error), "Could not write process standard input");
	else if (written != input_length) {
		dynlex_runtime_set_error("Process standard input closed before all input was written");
		capture_runtime_error(first_error, sizeof(first_error), "Process standard input was only partially written");
	}
	if (dynlex_process_close_input(process) != 0 && first_error[0] == '\0')
		capture_runtime_error(first_error, sizeof(first_error), "Could not close process standard input");
	if (dynlex_platform_process_pump(process, true, 0) != 0 && first_error[0] == '\0')
		capture_runtime_error(first_error, sizeof(first_error), "Could not wait for process");
	if (first_error[0] == '\0')
		return 0;
	dynlex_runtime_set_error(first_error);
	return -1;
}

size_t dynlex_process_output_size(DynlexProcess *process, int32_t stream_value) {
	if (process == NULL || (stream_value != DYNLEX_PROCESS_STREAM_STDOUT && stream_value != DYNLEX_PROCESS_STREAM_STDERR))
		return 0;
	dynlex_platform_process_lock(process);
	size_t length = output_buffer(process, (DynlexProcessStream)stream_value)->length;
	dynlex_platform_process_unlock(process);
	return length;
}

size_t dynlex_process_copy_output(DynlexProcess *process, int32_t stream_value, char *buffer, size_t capacity) {
	if (process == NULL || buffer == NULL ||
		(stream_value != DYNLEX_PROCESS_STREAM_STDOUT && stream_value != DYNLEX_PROCESS_STREAM_STDERR))
		return 0;
	dynlex_platform_process_lock(process);
	DynlexProcessBuffer *captured = output_buffer(process, (DynlexProcessStream)stream_value);
	size_t copied = captured->length < capacity ? captured->length : capacity;
	if (copied > 0)
		memcpy(buffer, captured->data + captured->offset, copied);
	dynlex_platform_process_unlock(process);
	return copied;
}

size_t dynlex_process_error_message(char *buffer, size_t capacity) { return dynlex_runtime_error_message(buffer, capacity); }

int32_t dynlex_process_was_launched(DynlexProcess *process) {
	dynlex_runtime_clear_error();
	if (process != NULL && process->launched)
		return 1;
	if (process != NULL)
		dynlex_runtime_set_error(process->launch_error);
	else
		dynlex_runtime_set_error("Could not allocate process launch result");
	return 0;
}

size_t dynlex_process_launch_error(DynlexProcess *process, char *buffer, size_t capacity) {
	const char *message = process == NULL ? "Could not allocate process launch result" : process->launch_error;
	size_t length = strlen(message);
	if (buffer != NULL && capacity > 0) {
		size_t copied = length < capacity - 1 ? length : capacity - 1;
		memcpy(buffer, message, copied);
		buffer[copied] = '\0';
	}
	return length;
}

int32_t dynlex_process_platform_is_windows(void) {
#ifdef _WIN32
	return 1;
#else
	return 0;
#endif
}
