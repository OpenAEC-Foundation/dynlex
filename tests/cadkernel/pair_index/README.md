# Native geometric pair index

`lib/cadkernel/pair_index.dl` supplies a reusable ordered-i64-pair to nonnegative
native-index map. This support module is not counted as an additional upstream
CAD module. It avoids linear scans of all spatial buckets during graph welding.

The table reserves a power-of-two capacity at least twice the requested entry
bound (minimum eight slots), uses all 64 bits of each key, resolves collisions
with bounded linear probing and returns -1 for absence. Values must be
nonnegative. Construction accepts bounds from zero through 536,870,912; larger
or negative bounds abort before growth. A full-table miss terminates, and a
full-table insertion aborts. Updates to existing keys remain valid when full.
Managed handles share mutable storage and keep it alive until the last copy is
released. A reset handle cannot be queried.

Four native groups cover 1,024 wide keys, ordering, copy/update/reset lifetime,
local returns, signed-key extrema, collisions and full-table misses. Five
additional fixtures verify explicit runtime refusal, under unattended Windows
error handling. All six fixtures pass at O0/O2 through required wrappers.
See `build/pair-index-fixtures.log`, `build/pair-index-refusals.log` and
`build/arrangement2-pair-index-required.log`.
