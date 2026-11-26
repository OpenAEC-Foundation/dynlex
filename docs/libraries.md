# Project libraries

In VS Code, open your application folder and use **Explorer → Libraries → +**.
Search the catalog, choose a library and its import name, then select **Add**.
The same picker accepts a Git repository, a source SDK archive URL, or a local
library folder. A missing library import also offers **Add library to project**
through the editor's quick fixes. Each installed library has Update, Remove and
View Documentation actions. Restore Libraries recreates files from the lock.

In Studio, choose **+ Add Library** in the project panel. Search and select a
library, choose its import name and use **Add selected library**. The panel also
accepts archive and GitHub repository URLs and a local folder. Installed library
sources are read-only and appear under Libraries. Dependencies belong to the
current program or shader project and its saved draft.

For example, adding DynLex UI as `ui` makes these public imports available:

```dl
import ui/native.dl
```

Use `ui/browser.dl` for a browser application. Native windows require the native
graphics dependencies; adding a source library does not install system packages.

Projects keep dependency requests in `dynlex.json` and exact revisions, content
hashes, exports and transitive dependencies in `dynlex.lock`. Commit both files.
The generated `.dynlex/` directory is ignored by Git. Libraries are installed
under `.dynlex/packages/<package-name>`; aliases select their declared exports.
The compiler, language server and debugger all read the same project metadata,
discovered from the source location independently of the working directory.
Use `./helper.dl` for an explicitly relative application import.

Libraries declare their name, public exports, dependencies and distributed files
in `dynlex-package.json`. Source SDK archives carry a verified `sdk.json` file
listing SHA-256 hashes of every source and asset. Installation runs no package
scripts. Git dependencies record the resolved commit. Local path dependencies
keep their path and record the installed content snapshot; Update refreshes it.
Removing a dependency also removes packages that are no longer reachable.

Browser package files and assets are cached in IndexedDB; saved projects retain
their dependency metadata rather than duplicating the SDK in local storage.
Local browser folders are copied into that cache. Updating one asks for its folder
again. Browser Git access supports GitHub; other servers can distribute SDK ZIP
archives over HTTP with CORS enabled.

The editor interfaces share `packages/manager.mjs`. For terminal use, run
`node packages/cli.mjs add <catalog-name|URL|path> --as <alias>` from the compiler
checkout with the application as the working directory (use the script's absolute
path). The script also supports list, update, remove, restore and catalog.
`DYNLEX_LIBRARY_CATALOG` selects a catalog for the command-line script;
`dynlex.libraries.catalog` configures the VS Code catalog.
