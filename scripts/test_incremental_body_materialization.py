"""Cached semantic facts and native IR do not require source-body clones."""


import json
from pathlib import Path


def check_body_materialization(root, build):
    main = root / 'body_materialization.dl'
    source = ('to emit {value}:\n'
              '    @intrinsic("variadic call", "libc", "printf", '
              '@intrinsic("type", "nothing"), 1, "%d\\n", value)\n'
              'emit @intrinsic("command line argument count")\n'
              '@intrinsic("discard", 1)\n')
    main.write_text(source)
    cold = build(entry=main)
    assert cold[2] == b'0\n'
    assert cold[0]['source_function_body_clones'] > 0
    for value in (2, 3):
        main.write_text(source.replace('"discard", 1', f'"discard", {value}'))
        edited = build(entry=main)
        assert edited[1:] == build(False, entry=main)[1:]
        assert edited[0]['function_body_inference_passes'] == 0
        assert edited[0]['source_function_body_clones'] == 0, \
            'cached facts/IR cloned the unchanged source body'
    main.write_text(source.replace('"%d\\n"', '"value=%d\\n"'))
    changed = build(entry=main)
    assert changed[2] == b'value=0\n'
    assert changed[1:] == build(False, entry=main)[1:]
    assert changed[0]['function_body_inference_passes'] == 1
    assert changed[0]['source_function_body_clones'] == 1

    # A new argument needs the saved typed body, not a fresh source template.
    main = root / 'body_materialization_pure.dl'
    source = ('to get next {value}:\n'
              '    @intrinsic("return", @intrinsic("add", value, 1))\n'
              '@intrinsic("variadic call", "libc", "printf", '
              '@intrinsic("type", "nothing"), 1, "%d\\n", next 1)\n'
              '@intrinsic("discard", 1)\n')
    main.write_text(source)
    assert build(entry=main)[2] == b'2\n'
    main.write_text(source.replace('"discard", 1', '"discard", 2'))
    observed = build(entry=main)
    assert observed[1:] == build(False, entry=main)[1:]
    assert observed[0]['source_function_body_clones'] == 0
    assert observed[0]['function_body_inference_passes'] == 0
    assert observed[0]['inference_bodies_loaded'] == 0
    for value in (2, 3):
        main.write_text(source.replace('next 1)', f'next {value})'))
        edited = build(entry=main)
        assert edited[2] == f'{value + 1}\n'.encode()
        assert edited[1:] == build(False, entry=main)[1:]
        assert edited[0]['function_body_inference_passes'] == 0
        assert edited[0]['inference_bodies_loaded'] > 0
        assert edited[0]['source_function_body_clones'] == 0


    check_nested_body_reactivation(root, build)
    check_known_pure_runtime_result(root, build)
    check_mutable_runtime_condition(root, build)
    check_nested_diagnostic_body_eviction(root, build)
    check_managed_runtime_effects(root, build)


def check_nested_body_reactivation(root, build):
    for name, inner, invocation in (
        ('ordinary', '@intrinsic("return", @intrinsic("add", value, 1))', 'inner value'),
        ('constant_parameter', '@intrinsic("return", @intrinsic("cast", value, '
         '@intrinsic("type", "int", size)))', 'inner 8 value'),
    ):
        main = root / ('nested_body_reactivation_' + name + '.dl')
        parameters = ('{@intrinsic("fix", @intrinsic("type", "int")):size} {value}'
                      if name == 'constant_parameter' else '{value}')
        source = (f'to get inner {parameters}:\n    {inner}\n'
                  f'to get outer {{value}}:\n    @intrinsic("return", {invocation})\n')
        call = '@intrinsic("discard", outer @intrinsic("command line argument count"))\n'
        inactive = source + '@intrinsic("discard", 0)\n'
        directory = root / 'cache' / 'native-v1'
        before = set(directory.glob('inference-body-*.entry'))
        main.write_text(source + call)
        build(entry=main)
        bodies = set(directory.glob('inference-body-*.entry')) - before
        for _ in range(2):
            main.write_text(inactive)
            removed = build(entry=main)
            assert removed[1:] == build(False, entry=main)[1:]
            main.write_text(source + call)
            restored = build(entry=main)
            assert restored[1:] == build(False, entry=main)[1:]
            assert restored[0]['function_body_inference_passes'] == 0
            assert restored[0]['source_function_body_clones'] == 0
            assert restored[0]['inference_bodies_loaded'] >= 2
        # Only the nested callee's executable product disappears. Its
        # enclosing body and both sets of type facts remain reusable.
        main.write_text(inactive)
        build(entry=main)
        removed = []
        operation = 'add' if name == 'ordinary' else 'cast'
        for path in bodies:
            record = json.loads(path.read_bytes().split(b'\n', 2)[2])
            intrinsic = record['expression_fields'].index('intrinsic')
            if any(expression[intrinsic] == operation for expression in record['expressions']):
                path.unlink()
                removed.append(path)
        assert len(removed) == 1
        main.write_text(source + call)
        restored = build(entry=main)
        assert restored[1:] == build(False, entry=main)[1:]
        assert restored[0]['function_body_inference_passes'] == 1
        assert restored[0]['inference_bodies_loaded'] == 1


def check_known_pure_runtime_result(root, build):
    source = ('to get next {value}:\n'
              '    @intrinsic("return", @intrinsic("add", value, 1))\n')
    for name, argument, expected in (('stored', 'item', b'2\n'),
                                     ('effect', 'with effect item', b'effect\n2\n')):
        main = root / ('known_pure_runtime_result_' + name + '.dl')
        effect = ('to get with effect {value}:\n'
                  '    @intrinsic("discard", @intrinsic("call", "libc", "puts", @intrinsic("type", "int"), "effect"))\n'
                  '    @intrinsic("return", value)\n') if name == 'effect' else ''
        text = source + effect + ('@intrinsic("store", item, 1)\n'
                         '@intrinsic("variadic call", "libc", "printf", @intrinsic("type", "nothing"), '
                         '1, "%d\\n", next ' + argument + ')\n@intrinsic("discard", 0)\n')
        main.write_text(text)
        first = build(entry=main)
        assert first[2] == expected
        main.write_text(text.replace('"discard", 0', '"discard", 1'))
        edited = build(entry=main)
        assert edited[1:] == build(False, entry=main)[1:]
        assert edited[2] == expected
        assert edited[0]['function_body_inference_passes'] == 0
        assert edited[0]['source_function_body_clones'] == 0


def check_nested_diagnostic_body_eviction(root, build):
    main = root / 'nested_diagnostic_body_eviction.dl'
    source = ('function return value:\n    replacement: @intrinsic("return", value)\n'
              'to get left - right:\n    return @intrinsic("subtract", left, right)\n'
              'to get value adjusted:\n    return value\n'
              'to get the tested form of value:\n    return value - 1 adjusted\n'
              'to get outer {value}:\n    return the tested form of value\n')
    call = '@intrinsic("discard", outer @intrinsic("command line argument count"))\n'
    main.write_text(source + call)
    first = build(entry=main)
    assert first[1].count('Warning:') == 1
    manifest_path = root / 'cache' / 'native-v1'
    manifest = json.loads(Path(first[0]['cache_entry']).read_bytes().split(b'\n', 2)[2])

    def payload(field):
        return json.loads((manifest_path / ('payload-' + manifest['payload_keys'][field] + '.entry'))
                          .read_bytes().split(b'\n', 2)[2])

    projection = payload('all_function_projection')
    section, = [path for path, declaration in projection['function_declarations'].items()
                if declaration['identity']['header'] == 'the tested form of value:']
    bodies = {reference['body']['payload'] for reference in payload('inference_summaries')[section].values()
              if reference['body'] is not None}
    assert len(bodies) == 1
    main.write_text(source + '@intrinsic("discard", 0)\n')
    assert not build(entry=main)[1]
    for body in bodies:
        (manifest_path / ('inference-body-' + body + '.entry')).unlink()
    main.write_text(source + call)
    restored = build(entry=main)
    assert restored[1:] == build(False, entry=main)[1:]
    assert restored[1].count('Warning:') == 1, 'runtime body preparation republished a retained diagnostic'


def check_mutable_runtime_condition(root, build):
    main = root / 'mutable_runtime_condition.dl'
    source = ('flex section while condition:\n    replacement: @intrinsic("loop while", condition)\n'
              'to get below {left} {right}:\n    @intrinsic("return", @intrinsic("less than", left, right))\n'
              '@intrinsic("store", index, 0)\n'
              'while below index 2:\n    @intrinsic("store", index, @intrinsic("add", index, 1))\n'
              '@intrinsic("variadic call", "libc", "printf", @intrinsic("type", "nothing"), 1, "%d\\n", index)\n')
    main.write_text(source)
    assert build(entry=main)[2] == b'2\n'
    main.write_text(source.replace('below index 2', 'below index 3'))
    changed = build(entry=main)
    assert changed[2] == b'3\n'
    assert changed[1:] == build(False, entry=main)[1:]


    # A type-valued call consumes the argument type even when its value changes.
    main = root / 'meta_runtime_call.dl'
    source = ('to get the kind of {value}:\n    @intrinsic("return", @intrinsic("type of", value))\n'
              '@intrinsic("discard", the kind of @intrinsic("command line argument count"))\n'
              '@intrinsic("discard", 0)\n')
    main.write_text(source)
    assert build(entry=main)[1:] == build(False, entry=main)[1:]
    main.write_text(source.replace('"discard", 0', '"discard", 1'))
    assert build(entry=main)[1:] == build(False, entry=main)[1:]


def check_managed_runtime_effects(root, build):
    folder = root / 'managed_runtime_effects'
    folder.mkdir()
    main = folder / 'main.dl'
    plain = 'class:\n    patterns:\n        resource\n    members:\n        value\n'
    lifecycle = ('    retain:\n        @intrinsic("discard", @intrinsic("lifecycle value"))\n'
                 '    release:\n        @intrinsic("discard", @intrinsic("call", "libc", "puts", '
                 '@intrinsic("type", "int"), "released"))\n')
    function = ('to get produce value:\n'
                '    @intrinsic("store", local, @intrinsic("construct", resource, 1))\n'
                '    @intrinsic("return", 1)\n')
    source = function + '@intrinsic("discard", produce value)\n'
    for declaration, expected in ((plain, b''), (plain + lifecycle, b'released\n'),
                                  (plain + lifecycle.replace('released', 'updated'), b'updated\n'),
                                  (plain, b'')):
        main.write_text(declaration + source)
        result = build(entry=main)
        assert result[2] == expected, 'folding a result discarded user-defined lifetime effects'
        assert result[1:] == build(False, entry=main)[1:]

    # Constructor temporaries also release their owned values before returning.
    temporary = function.replace('@intrinsic("store", local, @intrinsic("construct", resource, 1))',
                                 '@intrinsic("discard", @intrinsic("construct", resource, 1))')
    main.write_text(plain + lifecycle + temporary + '@intrinsic("discard", produce value)\n')
    result = build(entry=main)
    assert result[2] == b'released\n'
    assert result[1:] == build(False, entry=main)[1:]


    # The same operations on plain data stay pure. Managed storage contributes
    # implicit user-code calls even when the explicit body only copies values.
    from test_incremental_program_cache import read_program
    for managed, declaration in ((False, plain), (True, plain + lifecycle)):
        for body in ('@intrinsic("store", local, value)\n    @intrinsic("return", 1)',
                     '@intrinsic("return", value)',
                     '@intrinsic("discard", [value])\n    @intrinsic("return", 1)'):
            main.write_text(declaration + 'to get act {value}:\n    ' + body + '\n'
                            '@intrinsic("store", stored, @intrinsic("construct", resource, 1))\n'
                            '@intrinsic("discard", act stored)\n')
            result = build(entry=main)
            assert result[1:] == build(False, entry=main)[1:]
            program = read_program(root / 'cache', main)
            section, = [path for path, item in program['all_function_projection']['function_declarations'].items()
                        if item['identity']['header'] == 'act {value}:']
            summaries = program['inference_summaries'][section]
            assert summaries and all(summary['purity'] == int(managed) for summary in summaries.values()), \
                'implicit lifecycle calls were absent from the function effect product'
