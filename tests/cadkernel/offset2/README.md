# 2D offset verification

This directory verifies the native polyline offset implementation for
cadkernel revision `953d546b68aef4b6692566a1a9b077fc5bd9fb4f`.

The implementation conditions large coordinates around the first vertex,
normalises by the requested distance, splits arcs larger than a half turn,
offsets line and circular-arc segments, reconstructs sharp line joins and
circular mixed joins, and rejects collapsed inward loops. Closed and open
profiles retain their source closure, bulges and ownership behavior.

`main.dl` translates all ten source test groups. `verify.py` compares the
complete returned polyline lists with the pinned all-features implementation.
The corpus covers the source cases, open elbows, two-half and major-arc
circles, concave inward and outward profiles, survey coordinates, 64 seeded
random rectangles, 32 seeded open profiles and 24 seeded semicircles.

The recorded run passes 140 cases and 3,956 field comparisons across O0 and
O2. It contains 272 valid mode results and exact native optimization parity.
The native probe compiled in 4.141 seconds at O0 and 5.234 seconds at O2. The
compiler SHA-256 was
`38ebb889d576c9d9343f3a0cbbfa7025be611cef65a010e23662169b7457fc5a`.

Run the differential check with:

```powershell
python -B tests/cadkernel/offset2/verify.py `
  --source C:/path/to/cadkernel-at-953d546 `
  --compiler build/dynlex.exe `
  --output build/offset2-checks
```

This establishes the complete `src/geom2d/offset.rs` source scope at the
pinned revision. The implementation exposes the same empty-result behavior
for zero distances, insufficient vertices and collapsed inward profiles.
