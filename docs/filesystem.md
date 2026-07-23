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
create directory at path
```

Actions return `void` and set the subject to an action object. Test an action on the same line or afterward:

```dynlex
delete "b.txt" and print if it succeeded
```

Write, append, copy, rename, delete, and directory creation set a `file action`. Read sets a `file read action`. Both action types provide `succeeded` and `error message`; read actions also provide `contents`:

```dynlex
read "notes.txt" and print if it succeeded
print (the contents of it) as line
print (the error message of it) as line
```

The error message is empty after a successful action and contains the filesystem failure after an unsuccessful action. A failed read has empty contents. Delete accepts regular files and empty directories.

The module provides regular-file, readability, and modification-time queries:

```dynlex
print "notes.txt" is a regular file as line
print "notes.txt" is readable as line
print the modification time of "notes.txt" as line
```

`path is readable` is true only for a regular file that can be opened for reading; directories are not readable files. Modification time is a signed 64-bit count of milliseconds since the Unix epoch. It is zero when metadata for the path cannot be read.

Reads and writes preserve embedded zero bytes. Paths may be string values or C string literals. The module's runtime helpers and operation implementations are `local`; the action patterns, action types and properties, and path queries are public.

Native programs operate on the host filesystem. The browser runner provides an in-memory filesystem with files, directories, and modification times that persist between program runs for the lifetime of the compiler worker. Browser paths do not access the host filesystem and are discarded when that worker is replaced, such as when the page is reloaded.
