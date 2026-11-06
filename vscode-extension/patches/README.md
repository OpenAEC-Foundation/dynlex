The language client uses `Minimatch.makeRe()` only to check whether a pattern
compiled. Minimatch 11 removes that API and fixes GHSA-g3hv-75wg-m249. The patch
checks the compiled pattern set instead, preserving the existing false/error
results for patterns with no compiled alternatives. Matching still uses
`Minimatch.match()`.

`postinstall` applies the patch with `patch-package --error-on-fail`, including
on clean CI installs. The glob-selector and file-operation tests exercise both
patched callers. Remove the patch when an upstream language-client release
supports Minimatch 11; do not remove the security override first.
