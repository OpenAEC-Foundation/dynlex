# Angled two-face sheet thickening

This bounded native route thickens an open sheet made of two planar faces
meeting along one straight edge. It copies and thickens each face, extrudes the
source's miter connector along the common edge, then joins coincident full
faces or uses a Boolean union. It accepts positive and negative distances,
preserves the borrowed source, and requires one closed, valid solid on success.

The differential pins cadkernel revision
`953d546b68aef4b6692566a1a9b077fc5bd9fb4f` and checks the source hash
of `src/brep/thicken.rs`. It builds the Rust reference from that checkout.
Nine cases per O0/O2 mode compare acceptance, source immutability, topology
counts and every vertex coordinate. Six angled cases succeed; zero and
nonfinite distances and a closed source body are refused. One valid reflex
case has 24 vertices and 22 faces, so the verifier compares each result with
its corresponding Rust case rather than assuming a fixed box shape. The
required fixture exercises both distance signs, a zero-distance refusal,
closed topology and an unchanged source.

Run from the repository root:

```text
python -B tests/cadkernel/brep_thicken_multiface_angled/verify.py --source <pinned-source-checkout> --compiler build/dynlex.exe
python -B tests/cadkernel/verify.py --filter cadkernel_brep_thicken_multiface_angled --filter cadkernel_brep_thicken_multiface_planar
```

The coplanar two-face route remains separately covered. More than two faces,
curved shared edges, analytic nonplanar faces and NURBS patches are still
outside this bounded implementation of `src/brep/thicken.rs`.
