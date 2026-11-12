"""A compilation observes each loaded source once, including repeated imports."""

import json
from pathlib import Path


def check_source_observation_reuse(root, build):
    folder = root / 'source_observation_reuse'
    folder.mkdir()
    main = folder / 'main.dl'
    shared = folder / 'shared.dl'
    source = ('to shared:\n'
              '    @intrinsic("discard", @intrinsic("call", "libc", "puts", '
              '@intrinsic("type", "int"), "before"))\n')
    shared.write_text(source)
    imports = []
    for index in range(8):
        name = f'helper{index}.dl'
        (folder / name).write_text(f'import shared.dl\nto helper{index}: shared\n')
        imports.append(f'import {name}\n')
    main.write_text(''.join(imports) + 'helper0\n')
    cold = build(entry=main)
    assert cold[1:] == build(False, entry=main)[1:]
    manifest = json.loads(Path(cold[0]['cache_entry']).read_bytes().split(b'\n', 2)[2])
    existing = sum(value is not None for value in manifest['files'].values())
    assert cold[0]['file_observations_computed'] == existing, 'a loaded source was rehashed or recanonicalized'
    assert cold[0]['file_observations_reused'] >= 8, 'repeated imports did not reuse source observations'
    shared.write_text(source.replace('"before"', '"after"'))
    edited = build(entry=main)
    assert edited[1:] == build(False, entry=main)[1:]
    assert edited[2] != cold[2], 'a new compilation reused stale source contents'
    assert edited[0]['file_observations_computed'] == existing

    # A missing file is observed again in the next compiler process.
    main.write_text('import missing.dl\nmissing\n')
    failed = build(entry=main, success=False)
    assert failed[1:] == build(False, entry=main, success=False)[1:]
    (folder / 'missing.dl').write_text('to missing: @intrinsic("discard", 0)\n')
    found = build(entry=main)
    assert found[1:] == build(False, entry=main)[1:]
