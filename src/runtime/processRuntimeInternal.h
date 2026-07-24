#ifndef DYNLEX_PROCESS_RUNTIME_INTERNAL_H
#define DYNLEX_PROCESS_RUNTIME_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
	char *data;
	size_t length;
} DynlexProcessString;

typedef struct {
	DynlexProcessString name;
	DynlexProcessString value;
	bool unset;
} DynlexProcessEnvironmentEntry;

typedef struct DynlexProcessCommand {
	size_t references;
	DynlexProcessString executable;
	DynlexProcessString working_directory;
	bool has_working_directory;
	bool inherit_environment;
	DynlexProcessString *arguments;
	size_t argument_count;
	size_t argument_capacity;
	DynlexProcessEnvironmentEntry *environment;
	size_t environment_count;
	size_t environment_capacity;
	char configuration_error[512];
} DynlexProcessCommand;

typedef struct {
	char *data;
	size_t offset;
	size_t length;
	size_t capacity;
} DynlexProcessBuffer;

typedef enum {
	DYNLEX_PROCESS_STREAM_STDOUT = 1,
	DYNLEX_PROCESS_STREAM_STDERR = 2,
	DYNLEX_PROCESS_STREAM_ANY = 3,
} DynlexProcessStream;

typedef struct DynlexProcess {
	size_t references;
	void *platform;
	DynlexProcessBuffer standard_output;
	DynlexProcessBuffer standard_error;
	bool launched;
	bool standard_output_closed;
	bool standard_error_closed;
	bool finished;
	bool cleaned;
	bool termination_requested;
	bool cleanup_ready;
	int64_t exit_code;
	int32_t termination_signal;
	char launch_error[512];
} DynlexProcess;

int dynlex_process_append_output(DynlexProcess *process, DynlexProcessStream stream, const char *data, size_t length);
void dynlex_process_mark_stream_closed(DynlexProcess *process, DynlexProcessStream stream);
void dynlex_process_mark_finished(DynlexProcess *process, int64_t exit_code, int32_t termination_signal);

int dynlex_platform_process_launch(DynlexProcess *process, const DynlexProcessCommand *command);
int dynlex_platform_process_pump(DynlexProcess *process, int64_t timeout_milliseconds, DynlexProcessStream requested_stream);
int dynlex_platform_process_write(DynlexProcess *process, const char *data, size_t length, size_t *written);
int dynlex_platform_process_close_input(DynlexProcess *process);
int dynlex_platform_process_terminate(DynlexProcess *process);
int dynlex_platform_process_kill(DynlexProcess *process);
int dynlex_platform_process_cleanup(DynlexProcess *process);
void dynlex_platform_process_destroy(DynlexProcess *process);
void dynlex_platform_process_lock(DynlexProcess *process);
void dynlex_platform_process_unlock(DynlexProcess *process);

#endif
