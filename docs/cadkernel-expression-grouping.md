# Nested expression grouping regression

`tests/required/nested_division_grouping` is a standalone DynLex regression
with no imports. It evaluates `ceiling(numerator / first / second)` inside a
`maximum` call. For `1.5`, `3.0` and `0.17453292519943295`, the left-associated
calculation rounds up to `3`. The prior compiler produced `1` at both O0 and
O2 because a later grouping with the same *local* ordering replaced the first
prioritized grouping while resolving the nested expression.

`src/cpp/compiler/type_inference/operand_reordering_inference.inl` now rolls
back that later candidate and keeps the first selection. This follows the
existing rule that locally equivalent alternatives at an opaque boundary are
not a distinct ambiguity. The change is in general expression inference;
there is no CAD-specific parsing or arithmetic path.

The regression matches `expected.txt` with a rebuilt compiler at O0 and O2.
The deformed path-sweep fixture and its expression fixture pass at both
optimization levels with the source-shaped nested calculation. The Rust
differential matches 36 cases and 1,982 fields at O0/O2, including twist and
arc subdivisions.

The available non-CAD language-fixture run reported 501 passes among 527
fixtures. All 26 failures were reproduced with the preceding compiler binary
in the same environment; this comparison detects no new failure from the
grouping change, but it does not establish a clean full-suite baseline.
