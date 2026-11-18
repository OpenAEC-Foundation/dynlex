# Browser projects

The browser compiler accepts a complete, explicit project snapshot. Application
libraries, including dynlex-ui, are supplied by the caller rather than embedded
in compiler syntax or code generation.

```js
compiler.ccall('dynlex_web_set_import_sources', 'number', ['string'], [JSON.stringify({
  'main.dl': 'import vocabulary.dl\nprint line 7 doubled\n',
  'vocabulary.dl': 'value doubled means: value * 2\n'
})]);
compiler.ccall('dynlex_web_set_entry_file', 'number', ['string'], ['main.dl']);
const status = compiler.ccall('dynlex_web_compile_and_emit_wasm', 'number');
```

Both setters return zero on success. Filenames must be normalized relative paths
without traversal, empty components or file/directory collisions. Invalid input
is rejected before changing the installed snapshot. Replacing a snapshot removes
its former files and empty directories. `dynlex_web_init` also clears installed
files. The inline-source API explicitly selects its own main source again.

`src/web/compiler` exposes these operations through isolated workers:

- `compiler`: compile a project to Wasm or shader stages; release unused artifacts.
- `program`: consume a Wasm artifact, stream output, and finish or cancel its worker.
- `language`: install project sources and exchange JSON-RPC messages with the
  embedded language server.

Compiler and language workers serialize operations on their mutable compiler
instances. Program workers have independent lifetimes, so an infinite program
does not block the interface or prevent Stop. Workers announce readiness before
their clients post queued requests. Internal failures are logged, while clients
receive the generic error message. User-source diagnostics remain structured
compiler results.

The website controller in [dynlex-website](https://github.com/OpenAEC-Foundation/dynlex-website), `site/editor/language.dl`, owns initialization, document
versions, analysis timing, diagnostics and token themes. The shared UI receives
only source-bound UTF-8 color spans. It does not know DynLex token classes or
the language-server protocol.

Run the project API checks with `node tests/web/project_sources.mjs`. Controller
checks use `python3 tests/web/site_runtime.py`; they compile separate DynLex test
programs and record platform requests without creating windows.
