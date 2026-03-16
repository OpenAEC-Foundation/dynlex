# DynLex Project Memory

## Known Issues
- `globals` test: compiler exits 0 but produces no binary — likely a codegen bug with global variables, not a pattern resolution issue. The test previously "passed" because a stale compiled binary was cached in the directory.
- `import` test: relative imports (`import other.dl`) don't resolve relative to the importing file's directory — they resolve from the working directory.

## Test Infrastructure
- Tests renamed from numbered (`0_simple`) to descriptive (`simple`) names
- `expected_error.txt` support added to `test.sh` for expected compilation failures (substring match)
- `tests/required/*/main` binaries are gitignored

## Pattern Specificity Rematching
- When un-resolving a reference, definitions that had VL→Variable promotions must be removed from the tree BEFORE reverting element types (tree was built with old types)
- `removePatternPart` recursively walks the tree and detaches empty nodes bottom-up
- Argument nodes on the less-specific path must stay in `nextLess` across multiple elements (absorbing) for sub-expression matching
