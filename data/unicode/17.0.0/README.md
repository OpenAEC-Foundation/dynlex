# Unicode property inputs

These are unchanged Unicode 17.0.0 data files:

- [CaseFolding.txt](https://www.unicode.org/Public/17.0.0/ucd/CaseFolding.txt)
- [DerivedGeneralCategory.txt](https://www.unicode.org/Public/17.0.0/ucd/extracted/DerivedGeneralCategory.txt)
- [Unicode License V3](https://www.unicode.org/license.txt)

`scripts/generate_unicode_properties.py` verifies their pinned SHA-256 hashes and
generates `lib/unicode_properties.dl`. Generation and `--check` work offline.
The generated module includes the full license notice.

Simple case folding uses the C and S mappings, without locale tailoring or
multi-character expansion. Identifier words contain letters, marks, numbers,
connector punctuation, and the two join controls U+200C/U+200D. This property is
not Unicode word segmentation or locale-specific dictionary segmentation.
