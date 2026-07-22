# Filesystem standard library

Import the module directly:

```dynlex
import lib/filesystem.dl
```

The module provides these file actions:

```dynlex
write contents to path
append contents to path
read path
copy source to destination
rename source to destination
delete path
```

Actions return `void` and set the subject to an action object. Test an action on the same line or afterward:

```dynlex
delete "b.txt" and print if it succeeded
```

Write, append, copy, rename, and delete set a `file action`, which has a boolean `succeeded` property. Read sets a `file read action`, which has `succeeded` and `contents` properties:

```dynlex
read "notes.txt" and print if it succeeded
print (the contents of it) as line
```

`path is readable` is a direct boolean query:

```dynlex
print "notes.txt" is readable as line
```

Reads and writes preserve embedded zero bytes. Failed reads provide an empty string and set `succeeded` to false. Paths may be string values or C string literals. The module's C-runtime helpers and operation implementations are `local`; only the action patterns, action types, success checks, and readability query are public.

Native programs operate on the host filesystem. The browser runner provides an in-memory filesystem that persists between program runs for the lifetime of the compiler worker. Browser files do not access the host filesystem and are discarded when that worker is replaced, such as when the page is reloaded.
