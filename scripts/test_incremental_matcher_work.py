"""Fingerprint each matcher request once and visit increasing literal paths."""


def check_matcher_fingerprint_work(root, build):
    for name, spelling, limit in (("single", "announce", 1), ("sorted", "zulu alpha middle", 10)):
        main = root / f"matcher_fingerprint_{name}.dl"
        declaration = f'to {spelling}: @intrinsic("discard", 0)\n'
        main.write_text(declaration + spelling)
        cold = build(entry=main)
        assert cold[0]["matcher_key_computations"] == 1, "recording a matcher result recomputed its input fingerprint"
        assert 0 < cold[0]["matcher_signature_lookups"] <= limit, "signature traversal revisited earlier literals"
        assert cold[1:] == build(False, entry=main)[1:]
        main.write_text(declaration + 'to elsewhere: @intrinsic("discard", 1)\n' + spelling)
        edited = build(entry=main)
        assert edited[0]["matcher_cache_hits"] >= 1, "unrelated definition changed the reference's candidate fingerprint"
        assert edited[1:] == build(False, entry=main)[1:]

