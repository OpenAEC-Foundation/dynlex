"""A folded named global retains the execution producing its value."""


def check_global_data_initializers(root, build):
    source = '''integer means: @intrinsic("type", "int", 32)
function initial:
    execute: @intrinsic("return", 42)
section answer {name} from {integer:initial value}:
    declares: name
    result: integer
    value: initial value
function inspect:
    globals: sample entry
    execute: @intrinsic("return", sample entry)
answer sample entry from initial:
@intrinsic("discard", @intrinsic("variadic call", "libc", "printf", integer, 1, "%d\\n", inspect))
'''
    provider = source.replace('"return", 42', '"return", 43')
    provider_again = source.replace('"return", 42', '"return", 44')
    argument = provider_again.replace('sample entry from initial:', 'sample entry from 45:')
    renamed = argument.replace('sample entry', 'another answer')
    for level in ('-O0', '-O2'):
        main = root / f'global_data_initializers_{level[1:]}.dl'
        for index, (text, expected) in enumerate((
            (source, b'42\n'), (provider, b'43\n'), (provider_again, b'44\n'),
            (argument, b'45\n'), (renamed, b'45\n'), (renamed, b'45\n'),
            (source, b'42\n'),
        )):
            main.write_text(text)
            clean = build(False, entry=main, flags=(level,))
            assert clean[1:] == ('', expected)
            edited = build(entry=main, flags=(level,))
            assert edited[1:] == clean[1:], (index, level, edited[1:], clean[1:])
            if index == 5:
                assert edited[0]['frontend_reused']
                assert edited[0]['objects_built'] == 0
                assert edited[0]['link_ms'] == 0
