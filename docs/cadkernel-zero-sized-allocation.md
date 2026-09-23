# Zero-sized collection allocation regression

The spline differential probe supports zero-dimensional points, represented as `[f64; 0]`. Their size is zero. The shared checked allocator in `lib/std.dl` previously evaluated `maximum allocation bytes / element size` unconditionally. Creating a nonempty list of these points therefore divided by zero before any spline mathematics ran.

On Windows at O0 this terminated with `0xC0000094`; the modal application-error dialog made the parent runner appear to time out. At O2 the corresponding regression terminated with status 134. Neither outcome is an expected numerical refusal. This is a DynLex standard-library defect, separate from the pending recursive-action compiler regression.

The allocator now distinguishes logical element count from physical byte count. A positive number of zero-sized elements gets one valid owned byte, regardless of capacity. Nonzero-sized elements retain the overflow check and exact byte calculation. The implementation uses the same allocation and null-check path for both. It never divides by zero or requests `realloc(p, 0)`; nonpositive counts remain rejected. No C++ compiler code changed.

## Verification on 2026-09-12

Compiler base: `332dac1385edcbe6458386a5119b4cc62d010581`, with the corrected local `lib/std.dl`. Rust source reference: `953d546b68aef4b6692566a1a9b077fc5bd9fb4f`.

- Before the fix, the new `list_zero_sized_elements` fixture failed at runtime with native status `3221225620` at O0 and status `134` at O2. It now passes both. It exercises growth, insertion, replacement, removal, clear, reuse, release and a logical capacity of `2147483647` with constant physical storage.
- `checked_allocation_zero_count` and `checked_allocation_overflow` still produce their expected abort at both levels: six new fixture/mode checks pass in total.
- Eight existing fixtures pass at both levels: `list_library`, `list_multiple_element_types`, `list_checked_allocation`, `map_set_library`, `zero_length_array_arithmetic`, `managed_array_pointer_lifecycle`, `cadkernel_arena` and `cadkernel_spline` (16 fixture/mode checks).
- The rebuilt original spline probes match the pinned Rust probe for open and periodic interpolation of four points in dimensions 0, 1, 3 and 17 at both levels. The two dimension-zero cases each compare 17 output values. This is a targeted eight-case differential check, not a claim about the full spline matrix.

These fixtures run through the existing `verify_fixture` function in `tests/cadkernel/verify.py`; the three new fixtures are also discovered by `scripts/test.sh`. Their expectations were not relaxed to accept crashes.

## Unattended Windows child processes

`scripts/process_error_mode.py` temporarily adds `SEM_FAILCRITICALERRORS` and `SEM_NOGPFAULTERRORBOX` to the test launcher's process error mode. Child processes inherit these flags. The context restores the original parent mode, leaves system settings untouched and preserves failure status. The standard required-test launcher and the CAD fixture/differential launchers use this shared helper.

The two helper tests pass. They verify a normal nonzero exit, inherited flags, nested contexts, restoration after exceptions and transport of a native failure status. The pre-fix native allocation repro also returned its real crash status promptly through the guarded launcher. Microsoft documents the [inherited process error mode](https://learn.microsoft.com/en-us/windows/win32/api/errhandlingapi/nf-errhandlingapi-seterrormode) and [its use by unattended test harnesses](https://devblogs.microsoft.com/oldnewthing/20160204-00/?p=92972).

Run the helper tests with `python -B scripts/test_process_error_mode.py`. Run the full required suite through `scripts/test.sh` when its compiler-regression gate is ready; this correction does not establish a full-suite pass.
