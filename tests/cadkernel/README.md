# cadkernel fixture verification

`verify.py` discovers current and future `tests/required/cadkernel_*` fixture directories and tests each `main.dl` with explicit `-O0` and `-O2` compilation. It uses the already-built compiler; it does not build or modify the compiler, edit fixture expectations, or update port coverage.

The delivery remains one complete main-crate port: 83 upstream source files and 672 upstream tests, including optional modules. A passing selected-fixture run is not evidence that all of those source modules or upstream tests have been ported. The separate constraint-solver crate is outside that delivery. See [coverage](../../docs/cadkernel-coverage.md) and the separate [pinned Rust numerical reference](reference/README.md).

## Running

Use Python 3.10 or newer; only the standard library is required. From the repository root:

```sh
python -B tests/cadkernel/verify.py
python -B tests/cadkernel/verify.py --list
python -B tests/cadkernel/verify.py --filter 'cadkernel_core' --filter 'cadkernel_arena*'
python -B tests/cadkernel/verify.py --filter '*plane*' --optimization O2 --verbose
python -B tests/cadkernel/verify.py --compile-timeout 60 --run-timeout 20 --fail-fast
```

On Windows, the default compiler is `build/dynlex.exe`; elsewhere it is `build/dynlex`. Override with `--compiler PATH`. Relative compiler paths are resolved against the repository root, and the script can be invoked from another working directory. Select a Python executable installed on your host if `python` is not on PATH.

`--filter` accepts case-sensitive fixture-name globs. Repeated filters select their union, with each fixture run once per requested optimization. Every filter must match at least one fixture: empty selection and mistyped filters fail instead of producing a zero-test success. `--optimization O0` and `--optimization O2` can be repeated; omitting the option runs both. `--list` does not need a compiler or create artifacts.

Each run creates a unique directory under ignored `build/cadkernel-verify/`. Each fixture-mode compilation produces its own `.out` executable, including on Windows. Executables are launched directly through Python subprocess, without a shell. Existing output is never reused. Artifacts are retained for diagnosis and must not be committed. The script writes no source or expectation files.

## Expectations and failure behavior

- `expected.txt`: compare combined runtime stdout/stderr. Match the existing required-test convention by ignoring CR characters and terminal newlines; other spaces and line contents remain significant. Runtime must exit zero unless explicit failure metadata is present.
- `expected_diagnostics.txt`: compare combined compiler stdout/stderr using the existing `scripts/diagnostic_expectations.py` normalizer. It removes source line numbers and repository path prefixes while retaining column locations. Expectations containing source line numbers are rejected. Without `expected.txt`, nonempty diagnostics mean compilation must be rejected without emitting a binary. With `expected.txt`, diagnostics may describe a successful compile and runtime output is checked too. Compiler crashes never count as expected diagnostic failures.
- `expected_runtime_failure.txt`: `exit:N` for a nonzero status from 1 through 255, or `abort` using the platform mapping in the existing suite. A runnable fixture still requires `expected.txt`, and its output is compared even when the expected nonzero exit occurs. This checks current tolerance-refusal fixtures without accepting unrelated errors.
- `arguments.txt`: one UTF-8 argument per line, preserving empty argument lines; no shell splitting. `standard_input.txt` supplies bytes, or `standard_input.bytes` supplies hexadecimal bytes with optional repetition such as `00*4`. Supplying both fails.
- `working_directory.txt` and `environment.txt`: use the required-suite metadata conventions. Relative runtime directories resolve against the repository root; environment entries use `NAME=value` and duplicate names are rejected.
- `stack_limit_kb.txt`: positive integer compiler stack limit on POSIX; validated but not applied on Windows, matching the existing suite's Windows behavior.

Additional `.dl` files in a fixture directory are not independently discovered. For example, a supplementary differential program needs its own driver; this runner owns `main.dl` and its existing required-test expectations.

Compile timeout defaults to 30 seconds and runtime timeout to 15 seconds. Values must be finite and positive. On timeout, the runner attempts process-tree termination (Windows `taskkill /T /F`, POSIX process group) and reports which phase timed out. If tree cleanup is denied, that error is reported and the directly owned process is killed through its process handle; successful descendant cleanup is not claimed. Cleanup has separate bounded waits. Expected nonzero statuses never excuse timeouts. Each result prints the fixture, optimization mode, compile/run timings and any output/diagnostic difference. It continues after failures by default; `--fail-fast` reports the remaining modes as not run.

Exit status is 0 when every selected fixture-mode passes, 1 for fixture/process failure, 2 for command-line or selection errors, and 130 for interruption. A missing expectation, missing output executable, unexpected diagnostic, invalid metadata, wrong runtime exit or output mismatch cannot be reported as passing. No `--update`, golden-output creation, or automatic coverage promotion is provided.

On Windows the test launchers use `scripts/process_error_mode.py` to suppress modal fault-reporting dialogs only for the launcher and its children. The parent's original process error mode is restored afterward. Crashes keep their nonzero native status and still fail verification; no system-wide error-reporting setting is changed. `scripts/test_process_error_mode.py` checks inheritance, restoration and exit-status preservation.

## Earlier execution snapshot: 2026-09-12, before allocator correction

Using the existing Windows compiler at base revision `332dac1385edcbe6458386a5119b4cc62d010581`, the unfiltered invocation discovered eleven fixtures and ran all twenty-two fixture-mode combinations. Result: **16 passed, 6 failed, 0 not run; exit 1**, in 31.250 seconds. This is a snapshot of an actively developed fixture set, not a fixed expected suite size.

| Fixture | O0 result | O2 result |
| --- | --- | --- |
| cadkernel_alignment | Missing `lib/cadkernel/alignment.dl` import | Same import failure |
| cadkernel_spline | Runtime timeout after 15 seconds; Windows taskkill reported access denied and the owned process was killed | Runtime exit 134 instead of 0 |
| cadkernel_tessellation | Recursive type inference did not converge | Same compiler failure |

The tessellation failure is the documented [recursive implicit-return compiler regression](compiler-regressions/README.md). Default discovery includes it; there is no exclusion or expected-failure override in this runner. See that regression record for the compiler investigation. These runner changes do not alter compiler or fixture implementation.

The spline timeout/crash above is superseded by the [zero-sized collection allocation correction](../../docs/cadkernel-zero-sized-allocation.md). The complete spline runner now passes all five upstream tests, 804 differential cases and 56,876 comparisons at O0/O2, including N=0 open/periodic interpolation. The separate recursive-inference repair makes the tessellation fixture pass at O0/O2 as well. Complete compiler-suite verification and review are still pending; these focused results do not imply that the unfiltered suite passes.

A separate foundation selection passed **10/10 fixture-mode combinations**, exit 0, in 6.625 seconds:

```sh
python -B tests/cadkernel/verify.py --filter cadkernel_core --filter cadkernel_angle --filter cadkernel_frame --filter cadkernel_arena --filter cadkernel_arena_wrong_key
```

This selection verifies the current vector, angle, frame, arena and typed-key rejection fixtures at both optimization levels. It does not establish complete arena ownership/API semantics, plane verification, tessellation success, or the 83-file/672-test delivery. Complete module claims belong in the main coverage manifest after their remaining requirements have been verified.

Runner checks also exercised unmatched filters, invalid timeout values, duplicate-filter selection, output/diagnostic mismatches, line-numbered diagnostic rejection, subprocess execution, and an actual timed-out child process. No expectation file was changed to obtain these results.
