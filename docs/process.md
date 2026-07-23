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
set result to run command
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

`run command` closes the child's standard input, waits for it, and captures
standard output and standard error separately. `run command using value`
writes the complete string first; string lengths are preserved, including
embedded zero bytes. A `process result` exposes:

- `launched`: whether the operating system created the process;
- `succeeded`: whether launch, pipe communication, and waiting succeeded
  (independent of the child's exit code);
- `standard output` and `standard error`;
- `exit code`, `terminated`, and `termination signal`;
- `error message` for a launch or communication error.

Use `launch command` for a long-lived child. The returned managed `process`
keeps its pipes and operating-system process handle alive:

```dynlex
set child to launch command
set sent to write the string form of "request\n" to child
set response to read the standard output of child
set errors to read available the standard error of child
set closed to close the standard input of child
set status to wait for child
```

Test `child launched` after launch. If it is false, `the starting error of
child` contains the command-configuration or operating-system launch error.

Blocking reads wait for data or end-of-stream. `read available ...` only pumps
currently available data. Both forms drain standard output and standard error
while waiting, and writes also drain both output pipes, so one full pipe cannot
deadlock the other. Read results contain `contents`, `succeeded`, and
`end of stream`; write results contain `byte count` and `succeeded`.

`poll child` reports status without waiting. `wait for child` waits and drains
both output pipes. Process ownership and waiting apply to the launched target,
not descendants it creates. Once that target exits, DynLex captures bytes
already buffered in its pipes and closes them; a descendant that inherited a
pipe cannot keep `poll` or `wait` blocked. `terminate child` requests
termination and leaves the exit status available to `wait`; `clean up child`
forcibly ends a running child, waits for it, and closes every pipe and process
handle. Managed process values also perform that cleanup when their final copy
leaves scope.

The module is native-only. Browser/WASM programs cannot create host processes.
