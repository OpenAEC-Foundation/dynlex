# ACIS history pyramid reconstruction

This bounded native route projects the codec's `SolidHistoryOperation::Pyramid`
onto the public `rebuild_body` and one-operation `rebuild_history` entry points
of cadkernel revision `953d546b68aef4b6692566a1a9b077fc5bd9fb4f`. The codec
revision is `70ac6da7cf149cea6398a3d8829dd5e48b485b96`. The differential
pins the exact source files by SHA-256 before comparing results at O0 and O2.

The API takes the base radius, top radius, height, integer side count and a
borrowed column-major transform. It first rejects fewer than three sides,
then constructs the pointed or truncated polygonal body, then parses the
placement. Failure `1` is `InvalidTransform`; failure `2` is
`InvalidParameters`. The result owns its B-rep body; read `value` only when
`valid` is true. A matrix list must contain exactly 16 binary64 elements.

The required fixture checks the two polygonal branches, closed topology,
invalid sides and dimensions, and error priority. The differential compares
the full returned B-rep trace, including arena keys, geometry, provenance,
Euler characteristic and flaws, against both unchanged public Rust routes.
The wrapper does not parse codec records or dispatch other history operations.

Run from the DynLex repository root with pinned source and codec checkouts:

```powershell
python -B tests/cadkernel/acis_history_pyramid/verify.py --source <cadkernel-checkout> --codec <codec-checkout>
```
