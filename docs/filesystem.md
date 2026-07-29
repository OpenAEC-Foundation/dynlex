# Filesystem standard library

Import the module directly:

```dynlex
import lib/filesystem.dl
```

The module provides these file actions:

```dynlex
write contents to the file at path
append contents to the file at path
read the file at path
copy the filesystem entry at source to destination
rename the filesystem entry at source to destination
delete the filesystem entry at path
create a directory at path
```

Actions return `void` and set the subject to an action object. Test an action on the same line or afterward:

```dynlex
delete the filesystem entry at "b.txt" and print whether it succeeded as a line
```

Write, append, copy, rename, delete, and directory creation set a `file action`.
Read sets a `file read action`. Both action types have `succeeded` and `failed`
predicates plus an `error message` property; read actions also provide
`contents`:

```dynlex
read the file at "notes.txt" and print whether it succeeded as a line
print the contents of it as a line
print the error message of it as a line
```

The error message is empty after a successful action and contains the filesystem failure after an unsuccessful action. A failed read has empty contents. Delete accepts regular files and empty directories.

The module provides regular-file and readability predicates. Detailed metadata
comes from a file-system entry:

```dynlex
print whether "notes.txt" is a regular file as a line
print whether "notes.txt" is readable as a line
set entry to the file system entry at "notes.txt"
print whether entry lookup succeeded as a line
if entry lookup succeeded:
    print whether entry's modification time is supported as a line
    if entry's modification time is supported:
        print (entry's modification time)'s seconds as a line
        print (entry's modification time)'s nanoseconds as a line
```

`path is readable` is true only for a regular file that can be opened for
reading; directories are not readable files. Check whether the entry lookup
succeeded and whether its modification time is supported before using the
timestamp's word-sized `seconds` and integer `nanoseconds` properties.

Reads and writes preserve embedded zero bytes. Paths may be string values or C string literals. The module's runtime helpers and operation implementations are `local`; the action patterns, action types and properties, and path queries are public.

Native programs operate on the host filesystem. The browser runner provides an in-memory filesystem with files, directories, and modification times that persist between program runs for the lifetime of the compiler worker. Browser paths do not access the host filesystem and are discarded when that worker is replaced, such as when the page is reloaded.
