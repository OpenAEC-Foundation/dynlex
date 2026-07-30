# Process standard library

Import the native process module directly:

```dynlex
import lib/process.dl
```

Create a command from one executable and add each argument separately. DynLex
passes the resulting argument vector directly to the operating-system process
API; it does not invoke a shell or interpolate metacharacters:

```dynlex
set command to a process command for "python3"
add "-c" as an argument to command
add "print('hello')" as an argument to command
run command and set result to it
```

Commands inherit the current environment by default. They can replace or
remove individual variables, select a working directory, or start from an
empty environment:

```dynlex
use "workspace" as the working directory of command
set environment variable "MODE" of command to "test"
unset environment variable "TOKEN" of command
do not inherit the environment in command
```

An executable containing a directory separator is used as a path. A bare
executable name is searched using `PATH` from the command's configured
environment, relative `PATH` entries are resolved against the selected working
directory, and launch fails if that environment has no `PATH`. Use an absolute
executable path for commands with an empty environment. On Windows,
root-relative (`\tool.exe`) and drive-relative (`C:tool.exe`) executable paths
and `PATH` entries are rejected because their meaning depends on ambient
per-drive current-directory state; use a fully qualified or ordinarily
relative path instead.

`run command` closes the child's standard input, waits for it, and sets the
subject to a result which captures standard output and standard error
separately. `run command using input`
writes the complete string first; string lengths are preserved, including
embedded zero bytes. A `process result` exposes:

- `result was launched`: whether the operating system created the process;
- `result succeeded` and `result failed`: whether launch, pipe communication, and waiting succeeded
  (independent of the child's exit code);
- `result's standard output` and `result's standard error`;
- `result's exit code`, `result reports termination`, and
  `result's termination signal`;
- `result's error message` for a launch or communication error.

Use `launch command` for a long-lived child. The action sets the subject to a
managed `process` which keeps its pipes and operating-system process handle
alive:

```dynlex
launch command and set child to it
write the string form of "request\n" to the standard input of child and set writing to it
read the standard output of child and set response to it
read the available standard error of child and set errors to it
close the standard input of child and set closure to it
wait for child and set status to it
```

Test `child was launched` after launch. If it is false, `the starting error of
child` contains the command-configuration or operating-system launch error.

Blocking reads wait for data or end-of-stream. `read the available ...` only
pumps currently available data. Both forms drain standard output and standard error
while waiting, and writes also drain both output pipes, so one full pipe cannot
deadlock the other. Read results provide `contents`, `succeeded`/`failed`, and
`reached the end of stream`; write results provide `byte count` and
`succeeded`/`failed`.

`poll child` sets the subject to its status without waiting. `wait for child`
waits and drains both output pipes. Process ownership and waiting apply to the launched target,
not descendants it creates. Once that target exits, DynLex captures bytes
already buffered in its pipes and closes them; a descendant that inherited a
pipe cannot keep `poll` or `wait` blocked. `terminate child` requests
termination and leaves the exit status available to `wait`; `clean up child`
forcibly ends a running child, waits for it, and closes every pipe and process
handle. Managed process values also perform that cleanup when their final copy
leaves scope.

The module is native-only. Browser/WASM programs cannot create host processes.
