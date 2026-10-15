# BIM geometry fixture verification

These fixtures accompany the reusable geometry selected from PR #8 into
`lib/bim`. They do not include Dynedra Lab or claim coverage of an entire
upstream CAD kernel. Per-file source references and MPL-2.0 notices are retained.
See `lib/bim/README.md` for the selected operations and their boundaries.

`python3 tests/cadkernel/verify.py` runs the retained `cadkernel_*` required
fixtures with explicit `-O0` and `-O2` compilation, using the already-built
compiler. It checks output, diagnostics and expected failures without creating
or changing expectations. Use `--filter GLOB` to select fixtures and
`--optimization O0` or `--optimization O2` for one optimization level.

The `cadkernel_*` fixture names preserve their source provenance. Their DynLex
imports use `lib/bim/`. Reference files beside individual fixtures document the
upstream results used to construct their expected output.
