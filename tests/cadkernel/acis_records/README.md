# In-memory SAT record model

This slice implements the data-model behavior used before geometry records are
interpreted. The native implementation is `lib/cadkernel/acis_records.dl`.
Its source contract is the pinned codec's
`src/entities/acis/types.rs` at `70ac6da7cf149cea6398a3d8829dd5e48b485b96`.
The consuming kernel is pinned at
`953d546b68aef4b6692566a1a9b077fc5bd9fb4f`.

Covered behavior:

- Default version and header, record defaults, append-assigned IDs and header
  count, lookup by record ID after storage reordering, and null resolution.
- All eleven token kinds, absolute token positions, pointer ordinals,
  coordinate-aware numeric slots, and numeric or keyword orientation.
- Numeric binary tags `0x02`, `0x03`, `0x04`, `0x05`, `0x06`, `0x15`, and
  `0x17`; packed coordinate tags `0x13`, `0x14`, and `0x16`; opaque preservation
  of unknown or malformed binary tokens. Binary pointer tag `0x0C` stays a
  binary token.
- Explicit deep cloning of token, record, and document values. Ordinary
  managed assignments share their lists; callers use the clone operation when
  they need an independent value. Record lookup results share token storage
  with the document and are read-only by convention. Document append stores an
  independent record.

`tests/required/cadkernel_acis_records` is the O0/O2 fixture. The separate
`verify.py` builds `reference.rs` against the pinned codec in offline mode,
checks both source revisions, then compares the reference trace with native
O0 and O2 results. For example:

```powershell
python -B tests/cadkernel/acis_records/verify.py --source <pinned-kernel-checkout> --codec <pinned-codec-checkout>
```

This slice does not parse or write SAT/SAB streams, decode binary counted-text
tags, resolve subtype blocks, expose typed geometry/topology views, or implement
append/lift/lower/history. Header version conversions, record mutation by ID,
and the remaining document convenience accessors also belong to later slices.
