# Complete diagnostic graph coverage

`main.dl` runs the complete fixture. Required tests separate `body.dl` from
`geometry.dl`: the former checks full nested graph fields, retained text and
body cloning; the latter checks all eight planar, six surface and five spatial
curve variants and their clones. `support.dl` constructs the shared spline data.
Every assertion from the combined fixture remains in one of these components.

These integration programs instantiate managed topology arenas and diagnostic
formatters together. Native O0/O2 compilation takes approximately 18–29 seconds
on the Windows validation host. The required wrappers declare a 60-second
compilation budget in `compile_timeout_seconds.txt`; runtime limits and exact
output assertions are unchanged. The suite validates every override as an
integer between 1 and 300 seconds. Fixtures without an override retain the
ordinary platform limit (10 seconds, or 20 on Windows).

Compiler phase measurements on the basic graph diagnostic fixture reached
pattern resolution at 2.2 seconds, inference at 7.2 seconds and code generation
at 15.4 seconds. LLVM-only output completed in 16.4 seconds; an isolated native
compilation completed in 20.4 seconds. The preceding required run reported
624 passes and two compilation timeouts, with no runtime/output failures.
This timing evidence does not count as a passing full-suite run.

The split wrappers pass all ten O0/O2 checks in `build/brep-split-check.log`.
The budget parser has positive/default/CRLF and malformed/out-of-range tests
in `scripts/test_fixture_options.py` and is part of the required suite.
