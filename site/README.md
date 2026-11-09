# DynLex website

The interface is declared in DynLex and rendered by the shared dynlex-ui library.
Page content and metadata come from the same records. Browser JavaScript supplies
platform capabilities, compiler workers and the Wasm execution host.

Build and serve from the compiler checkout:

```bash
python3 scripts/build_site.py
python3 -m http.server 7000 --directory build/site
```

Open `http://localhost:7000/` in a browser. The host prefers WebGPU and also
supports WebGL2. The build uses the checked-in browser compiler and pinned UI
source SDK; it does not open a browser or desktop window.

Page declarations live in `pages/`, with shared appearances in `appearance.dl`.
`interface.dl` composes the pages. Controllers in `programs/`, `editor/`,
`downloads/` and `shaders/` own behavior and retained state. `application.dl`
connects the interface, controllers and platform event stream.

To use changes from a dynlex-ui checkout, update the package and lock together:

```bash
python3 scripts/update_site_ui.py --ui /absolute/path/to/dynlex-ui
python3 scripts/build_site.py
```

Shared code links accept `code` or UTF-8 source encoded in `code64` using Base64
URL encoding, plus a `.dl` filename in `name`. `autorun=1` or `autorun=true` runs
the entry once when the source link changes. Repeated location notifications and
returning from another page retain edits and do not repeat autorun. A new entry
stops the previous program. Invalid links report a generic error and log details.

Controller tests run without windows:

```bash
python3 tests/web/site_runtime.py
python3 tests/web/site_runtime.py --case site_entry
node tests/web/project_sources.mjs
python3 tests/web/source_package.py
```

Migration is in progress. The current build contains the homepage, eleven
documentation pages, downloads, an explicit missing page and the initial IDE,
including live diagnostics and semantic colors. Game/account flows, shader
editing, further editor features, accessibility and deployment migration remain.
The existing deployment has not been replaced.
