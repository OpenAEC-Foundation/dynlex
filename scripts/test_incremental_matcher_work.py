"""Fingerprint each matcher request once and visit increasing literal paths."""

import json
from pathlib import Path


def check_matcher_fingerprint_work(root, build):
    for name, spelling, limit in (("single", "announce", 1), ("sorted", "zulu alpha middle", 10)):
        main = root / f"matcher_fingerprint_{name}.dl"
        declaration = f'to {spelling}: @intrinsic("discard", 0)\n'
        main.write_text(declaration + spelling)
        cold = build(entry=main)
        manifest_path = Path(cold[0]['cache_entry'])
        manifest = json.loads(manifest_path.read_bytes().split(b'\n', 2)[2])
        snapshot = manifest_path.parent / f"matcher-{manifest['matcher_key']}.entry"
        payload = snapshot.read_bytes().split(b'\n', 2)[2]
        assert cold[0]['matcher_snapshot_bytes'] == len(payload)
        repeated = build(entry=main)
        assert repeated[0]['frontend_reused']
        assert repeated[0]['matcher_snapshot_bytes'] == 0
        assert cold[0]["matcher_key_computations"] == 1, "recording a matcher result recomputed its input fingerprint"
        assert 0 < cold[0]["matcher_signature_lookups"] <= limit, "signature traversal revisited earlier literals"
        assert cold[1:] == build(False, entry=main)[1:]
        main.write_text(declaration + 'to elsewhere: @intrinsic("discard", 1)\n' + spelling)
        edited = build(entry=main)
        assert edited[0]["matcher_cache_hits"] >= 1, "unrelated definition changed the reference's candidate fingerprint"
        assert edited[1:] == build(False, entry=main)[1:]

