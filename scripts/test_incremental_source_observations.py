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
    existing = len({value['canonical'] for observations in ('files', 'configuration_files')
                    for value in manifest[observations].values() if value is not None})
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

    check_configuration_observations(root, build)


def check_configuration_observations(root, build):
    """Message-only configuration discovery preserves all compiled products."""
    warning = ('integer means: @intrinsic("type", "int")\n'
               'value means: 1\n'
               'to show {integer:value}: @intrinsic("discard", value)\n'
               'show 2\n')
    for name, source, success, key in (
        ('warning', warning, True, 'explicit parameter ambiguity'),
        ('error', 'missing\n', False, 'unresolved pattern'),
    ):
        folder = root / ('configuration_observations_' + name)
        nested = folder / 'nested'
        nested.mkdir(parents=True)
        main = nested / 'main.dl'
        main.write_text(source)
        target = nested / 'program.out'
        initial = build(entry=main, target=target, success=success)
        assert initial[1], 'fixture did not produce its diagnostic'
        output_time = target.stat().st_mtime_ns if success else None
        ancestor, closer = folder / 'config.dl', nested / 'config.dl'

        def settings(label):
            return f'dynlex options:\n    messages:\n        {key}: "{label}"\n'

        def check(label=None):
            result = build(entry=main, target=target, success=success)
            assert result[1:] == build(False, entry=main, target=target, success=success)[1:]
            assert result[0]['frontend_reused'], 'message-only configuration discovery reran the frontend'
            assert result[0]['objects_built'] == result[0]['link_ms'] == 0
            assert result[0]['cache_payloads_loaded'] == 0
            if label:
                assert label in result[1]
            else:
                assert result[1] == initial[1]
            if success:
                assert target.stat().st_mtime_ns == output_time, 'message-only configuration rewrote output'
            return result

        ancestor.write_text(settings('Ancestor wording'))
        check('Ancestor wording')
        closer.write_text(settings('Closer wording'))
        check('Closer wording')
        ancestor.write_text(settings('Updated ancestor wording'))
        check('Closer wording')
        closer.unlink()
        check('Updated ancestor wording')
        ancestor.unlink()
        check()

        # Invalid newly discovered configuration must be diagnosed before reuse.
        closer.write_text('dynlex options:\n    unknown setting: "invalid"\n')
        invalid = build(entry=main, target=target, success=False)
        assert invalid[1:] == build(False, entry=main, target=target, success=False)[1:]
        assert 'unknown config key' in invalid[1]
        closer.unlink()
        check()

    # A source file named config.dl remains a normal source dependency.
    folder = root / 'configuration_source_role'
    helper = folder / 'helper' / 'config.dl'
    helper.parent.mkdir(parents=True)
    helper.write_text('to shared: @intrinsic("discard", @intrinsic("call", "libc", "puts", '
                      '@intrinsic("type", "int"), "before"))\n')
    main = folder / 'main.dl'
    main.write_text('import helper/config.dl\nshared\n')
    first = build(entry=main)
    helper.write_text(helper.read_text().replace('"before"', '"after"'))
    changed = build(entry=main)
    assert not changed[0]['frontend_reused']
    assert changed[1:] == build(False, entry=main)[1:]
    assert changed[2] != first[2]
