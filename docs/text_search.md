# Literal text search

`lib/text_search.dl` searches UTF-8 text and returns original byte ranges:

```dynlex
import lib/text_search.dl

set result to literal text matches in "K k" for "k" matching case false requiring whole words false
for each match in (result's matches):
    print line (formatted "{match's start}..{match's end}")
# 0..3
# 4..5
```

Ranges use an inclusive start and exclusive end. Matches do not overlap. An
empty query returns no matches. The result owns an immutable sequence; copies
retain its ranges without retaining or copying the source text. Status 0 is
success, 1 reports malformed source UTF-8, and 2 malformed query UTF-8.
`error byte` identifies the invalid input byte, or is -1 on success.

Case-insensitive matching uses the Unicode 17.0.0 simple case-fold mappings C/S.
It is independent of the host locale and does not expand one character into
several, apply Turkic tailoring, or normalize canonically equivalent sequences.
`lib/unicode_properties.dl` also exposes `the Unicode simple case fold of code`
and `code is a Unicode word character`.

Whole-word matching requires the characters outside both ends of a match to be
absent or non-word characters. Word characters are letters, marks, numbers,
connector punctuation and join controls. This is an identifier-word property,
not dictionary or linguistic word segmentation.

The matcher builds a KMP prefix table over query code points. A bounded ring
records source byte offsets, so a fold that changes encoded width still produces
correct ranges. The matching pass is linear in source and query length, with
storage proportional to the query and returned matches. The original text is
not folded into another string. Unicode tables are immutable static arrays and
use binary lookup. They are generated offline from the pinned, licensed inputs
under `data/unicode/17.0.0`; `python3 scripts/generate_unicode_properties.py
--check` verifies them.

The integration fixture checks every Unicode code point against an independently
calculated database checksum. `node tests/web/text_search.mjs build/dynlex`
compares matching with an independent reference over every simple fold and a
deterministic mixed-script corpus. It runs native and WebAssembly builds at both
optimization levels and checks allocation ownership. `--shipped` also exercises
the browser compiler's embedded standard library.
