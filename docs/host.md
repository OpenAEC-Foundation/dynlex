# Native host standard library

Import the host module directly:

```dynlex
import lib/host.dl
```

Environment lookup distinguishes an absent variable from an empty value:

```dynlex
set home to the environment variable named "HOME"
if home failed:
    print home's error message as a line
else:
    if home was absent:
        print "HOME is not set" as a line
    else:
        print home's value as a line
```

Executable discovery searches the current process's `PATH` without launching
the program. A successful lookup can still report that the executable was
absent:

```dynlex
set tool to the executable named "rg"
print tool was found as a line
```

The module also exposes `the host platform name`,
`whether the process has administrator privileges`, and normal CLI output and
termination operations:

```dynlex
write "warning\n" to standard error
if it failed:
    print it's error message as a line
exit the program with status 1
```

Platform names currently returned by native builds are `Windows`, `macOS`,
`Linux`, `FreeBSD`, or `POSIX`. Browser builds report `Browser`; environment
and executable lookup and standard-error output return unsupported-operation
failures there. Exiting a browser program stops it with a WebAssembly runtime
error containing the requested status.
