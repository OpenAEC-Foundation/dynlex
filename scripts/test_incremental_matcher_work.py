"""Fingerprint each matcher request once and visit increasing literal paths."""

import json

from cache_payloads import read_payload
from pathlib import Path


def matcher_record_keys(segment, kinds=None):
    """Inspect selected matcher records without reconstructing compiler objects."""
    packed = segment.read_bytes().split(b"\n", 2)[2]
    header = b"DynLex matcher results v6\n"
    assert packed.startswith(header)
    position = len(header)
    result = set()
    while position < len(packed):
        key = packed[position:position + 64]
        position += 64
        length = shift = 0
        while True:
            byte = packed[position]
            position += 1
            length |= (byte & 127) << shift
            shift += 7
            if not byte & 128:
                break
        value = packed[position:position + length]
        position += length
        assert value
        if kinds is None or value[0] in kinds:
            result.add(key)
    assert position == len(packed)
    return result


def matcher_result_keys(segment):
    return matcher_record_keys(segment, {0, 1})


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
        descriptors = json.loads(payload)
        assert cold[0]['matcher_snapshot_bytes'] == len(payload) + sum(
            len((snapshot.parent / f"matcher-segment-{entry['payload']}.entry")
                .read_bytes().split(b"\n", 2)[2]) for entry in descriptors.values())
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
    check_matcher_segment_reuse(root, build)
    check_classification_segment_eviction(root, build)


def check_matcher_segment_reuse(root, build):
    main = root / "matcher_segments.dl"
    source = ('to alpha: @intrinsic("discard", 1)\n'
              'to beta: @intrinsic("discard", 2)\n'
              'to gamma: @intrinsic("discard", 3)\nalpha\nbeta\ngamma\n')

    def segments(result):
        manifest_path = Path(result[0]["cache_entry"])
        manifest = json.loads(manifest_path.read_bytes().split(b"\n", 2)[2])
        index = manifest_path.parent / f"matcher-{manifest['matcher_key']}.entry"
        descriptors = json.loads(index.read_bytes().split(b"\n", 2)[2])
        return {manifest_path.parent / f"matcher-segment-{entry['payload']}.entry"
                for entry in descriptors.values()}, index

    main.write_text(source)
    cold = build(entry=main)
    before, index = segments(cold)
    assert cold[0]["matcher_segments_built"] == len(before) > 1
    assert cold[0]["matcher_segments_reused"] == 0
    assert cold[0]["matcher_snapshot_bytes"] == sum(
        len(path.read_bytes().split(b"\n", 2)[2]) for path in before | {index})
    original_entries = {path: (path.stat().st_ino, path.read_bytes()) for path in before}
    main.write_text(source.replace('"discard", 1', '"discard", 4'))
    edited = build(entry=main)
    after, _ = segments(edited)
    assert edited[0]["matcher_segments_reused"] > 0, "body edit rewrote every matcher segment"
    assert edited[0]["matcher_segments_built"] < len(after)
    for path in before & after:
        assert original_entries[path] == (path.stat().st_ino, path.read_bytes()), \
            "body edit rewrote a retained matcher segment"
    assert edited[1:] == build(False, entry=main)[1:]

    # Segment filenames are content hashes. Arbitrarily deleting half can
    # remove every lookup while retaining only metadata, depending on paths.
    lookup_segments = {path for path in after if matcher_result_keys(path)}
    assert lookup_segments, "the fixture must contain cached matcher results"
    retained_lookup = min(lookup_segments)
    for path in after - {retained_lookup}:
        path.unlink()
    main.write_text(source.replace('"discard", 1', '"discard", 5'))
    evicted = build(entry=main)
    assert evicted[0]["matcher_segments_missing"] > 0
    assert evicted[0]["matcher_cache_hits"] > 0, "partial eviction discarded other matcher segments"
    assert evicted[1:] == build(False, entry=main)[1:]
    final, _ = segments(evicted)
    saved = {path: path.read_bytes() for path in final}
    try:
        for path, data in saved.items():
            path.write_bytes(data[:-1] + bytes([data[-1] ^ 1]))
        main.write_text(source.replace('"discard", 1', '"discard", 6'))
        failed = build(entry=main, success=False)
        assert "corrupt cache entry" in failed[1], "edited build ignored matcher segment corruption"
    finally:
        for path, data in saved.items():
            path.write_bytes(data)


def check_classification_segment_eviction(root, build):
    main = root / "classification_segment_eviction.dl"
    source = ('to consume {input}: @intrinsic("discard", input)\nconsume 1\n')
    main.write_text(source)
    cold = build(entry=main)
    manifest_path = Path(cold[0]["cache_entry"])
    directory = manifest_path.parent
    manifest = json.loads(manifest_path.read_bytes().split(b"\n", 2)[2])
    projection = read_payload(directory / f"payload-{manifest['payload_keys']['all_function_projection']}.entry")
    topology = projection["matcher_projection"]["classifications"]
    index = json.loads((directory / f"matcher-{manifest['matcher_key']}.entry")
                       .read_bytes().split(b"\n", 2)[2])
    assert len(index) > 1
    for prefix, entry in index.items():
        if prefix != topology[:2]:
            (directory / f"matcher-segment-{entry['payload']}.entry").unlink()
    main.write_text(source.replace('"discard", input', '"discard", @intrinsic("add", input, 1)'))
    edited = build(entry=main)
    assert edited[0]["matcher_segments_missing"] > 0
    assert edited[1:] == build(False, entry=main)[1:], "classification metadata eviction changed compilation"

