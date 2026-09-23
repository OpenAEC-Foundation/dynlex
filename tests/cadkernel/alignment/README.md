# Alignment verification

The reference imports complete, unchanged `src/space/alignment.rs` and
`src/space/vec.rs` from revision
`953d546b68aef4b6692566a1a9b077fc5bd9fb4f`. It checks the checkout revision and
diff before and after execution. The entire alignment source was reviewed,
including its one test and the end of the file; no production code follows the
test module.

Run from the repository root:

```text
python tests/cadkernel/alignment/verify.py --source PATH_TO_PINNED_CHECKOUT
```

Optional `--compiler PATH` and `--rustc PATH` select the native DynLex and Rust
compilers. Supplying the actual Rust compiler also avoids a launcher requiring
unavailable home-directory access. No Cargo dependencies or network are used.
All child processes use the existing guarded `run_process`/`verify_fixture`
utilities, so Windows crash dialogs cannot block an unattended test.

The runner executes the original Rust test, the unchanged
[`cadkernel_alignment` fixture](../../required/cadkernel_alignment/main.dl) at
O0/O2, then runtime differential inputs through both native probe binaries.
Reference geometry comes from the pinned module, not a translated Python
implementation. `reference.rs` applies matrices using the exact row-major
formula already present in the original Rust test.

Build artifacts, inputs and the Rust test log live in
`build/cadkernel-alignment-checks`. Its `results.json` records counts, mutation
evidence and SHA-256 hashes of the compiler, module, core/list/std dependencies
and test drivers. A stale success file is removed first. The runner refuses
success if those inputs change during verification.

## Complete operation and helper map

| Pinned source operation / branch | Native implementation | Evidence |
| --- | --- | --- |
| `align_point_pairs` | `the cad alignment from … to … scaling two pairs …` | Every matrix component, validity and mapped source/query points. |
| Count and input validation | Count/finite loops and local `cad alignment scalar … is finite` | All 0–4 count combinations; every source/target coordinate position receives nonfinite data. |
| One pair | Identity columns and computed translation | Translated origin, arbitrary query, signed zeros and translation overflow. |
| Baseline construction | Core vector difference/length/division | Both source and target at/below/above `1e-12`, underflow and overflow. |
| Two-pair Rodrigues expression | Local `the cad alignment rotation of … about … with cosine … and sine …` | General rotations, both sine branches and deterministic random baselines. |
| Antiparallel expression | Local `the cad alignment half turn of … about …` | Horizontal/vertical/oblique cases, including `abs(x.z)` near 0.9. |
| Parallel positive cosine | Unchanged identity columns | Parallel X/Y/Z and near-parallel cases. |
| Optional two-pair scale | Scale all three columns by `b_length/a_length` | Both flag values, unequal lengths, off-baseline query and extreme scales. |
| Three-pair normalization and frame | Cross/normalize for both frames; local `the cad alignment basis image of … from … to …` | Coincident/collinear/near-collinear third points on either side, original spatial frame and arbitrary spatial triples. |
| Three-pair scale policy | Scale code remains in the two-pair branch | Same triples with both flag values and unequal point spacing; lengths are preserved. |
| Translation and matrix assembly | Ordered subtraction and all 16 `cad alignment matrix` fields | Row-major coefficients, affine bottom row, off-axis points and large origins. |
| Final finite check / `None` | Finite columns/translation and local `no cad alignment` | Finite inputs that overflow baseline or translation still refuse; no matrix is consumed on failure. |
| Original test's matrix application | `the cad alignment of … through …` | Same left-associated row products; all source points plus independent query, including nonfinite query coordinates. |

The source's final scale-finiteness guard is retained. For ordinary IEEE
binary64, the earlier finite-length checks and `a_length > 1e-12` bound that
ratio far below overflow; the suite does not claim to force this unreachable
guard in isolation. It does exercise overflow in the resulting translation.

All matrices and vectors are value records. Source/target lists are borrowed;
the probes re-emit all coordinates after the call and release the lists once.
An invalid result's matrix is not treated as an answer. The local failure
constructor value-initializes its matrix, consistent with its documented zero
payload. No additional allocations occur inside alignment itself.

## Original assertion audit and independent fixtures

`three_pairs_map_a_spatial_frame` uses exactly the original three source and
target points and `scale_two_pairs=true`. The DynLex fixture first asserts a
valid result, then applies the matrix to **each of the three source points**
and checks Euclidean distance to the corresponding target `< 1e-12`.
This preserves the complete original test, including its loop and norm-based
assertion. Additional assertions check the known rotation entries, translations
9/7/6 and all four affine bottom-row values.

The existing fixture also independently checks one-pair translation, two-pair
rotation with/without uniform scale, three-pair scale exclusion, preferred axes
for horizontal and vertical half-turns, positive parallel identity, empty and
mismatched lists, duplicate baselines, inclusive length cutoff, collinear
triples, four-pair refusal, nonfinite input, overflow and caller ownership.
The new differential cases exercise every matrix entry and operation across a
larger deterministic input domain; they do not replace these expected-value
assertions.

## Precision and sensitivity

Printed values round-trip binary64. Finite results use relative tolerance
`3e-13` and **zero absolute tolerance** against Rust. No tolerance can hide
zero versus nonzero or a zero-sign mismatch. Validity, copied input coordinates,
the complete affine bottom row and output field order match exactly. Infinity
signs must agree; NaN comparison requires the same classification, not a payload
or sign. O0/O2 numeric comparison is exact under these signed-zero/NaN rules.
The query-point helper propagates IEEE arithmetic; it does not add the input
rejection performed by the alignment constructor.

Each full run compiles four faulty copies under `build/`: reversed translation,
the wrong half-turn axis, lost two-pair scaling and a strict baseline cutoff.
All must compile and then disagree with Rust in the selected output field;
compilation failure is not counted as detection. The original files remain
unchanged.

Verified dataset: **782 cases, 49,806 Rust comparisons and 24,903 exact O0/O2
pairs**, plus the unchanged Rust test and both required-fixture modes. Counts
include echoed coordinates for input preservation. The complete source review
and differential found no algorithmic discrepancy requiring a production edit.
This verifies alignment, not completion of the whole CAD port.
