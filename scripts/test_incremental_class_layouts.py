"""Class-instantiation state restored before cross-process constraint reuse."""


def check_class_layout_restoration(root, build):
    check_equivalent_recursive_layouts(root, build)
    check_class_symbol_identity(root, build)
    recursive = root / "recursive_class_constraint.dl"
    recursive_source = ('[a|] pointer to pointee means: @intrinsic("add pointer depth", pointee)\n'
                        'value as destination means: @intrinsic("cast", value, destination)\n'
                        'class:\n'
                        '    patterns:\n'
                        '        a node\n'
                        '    members:\n'
                        '        next as a pointer to a node\n'
                        'to consume {a node:value}: @intrinsic("discard", value)\n'
                        'to touch: @intrinsic("discard", 1)\n'
                        'touch\n'
                        'consume (@intrinsic("construct", a node, 0 as a pointer to a node))\n')
    recursive.write_text(recursive_source)
    build(entry=recursive)
    recursive.write_text(recursive_source.replace('"discard", 1', '"discard", 2'))
    edited = build(entry=recursive)
    assert edited[0]["constraint_values_reused"] >= 1, \
        "recursive class constraint was not restored across processes"
    assert edited[1:] == build(False, entry=recursive)[1:]

    variants = root / "class_layout_order.dl"
    source = ('class:\n'
              '    patterns:\n'
              '        a box\n'
              '    members:\n'
              '        value\n'
              'to consume {a box:box}: @intrinsic("discard", box)\n'
              'to touch: @intrinsic("discard", 1)\n'
              'touch\n'
              '@intrinsic("store", {integer box}, @intrinsic("construct", a box, 1))\n'
              '@intrinsic("store", {float box}, @intrinsic("construct", a box, 1.5))\n'
              'consume integer box\n'
              '@intrinsic("discard", the value of float box)\n'
              '@intrinsic("return", @intrinsic("subtract", the value of integer box, 1))\n')
    variants.write_text(source)
    build(entry=variants)
    variants.write_text(source.replace('"discard", 1', '"discard", 2'))
    edited = build(entry=variants)
    assert edited[0]["constraint_values_reused"] >= 1
    assert edited[1:] == build(False, entry=variants)[1:]

    provider = root / "class_field_type_provider.dl"
    source = ('floating means: @intrinsic("type", "float")\n'
              'integer means: @intrinsic("type", "int")\n'
              'class:\n'
              '    patterns:\n'
              '        a point\n'
              '    members:\n'
              '        x as integer\n'
              'to observe {floating:value}: @intrinsic("discard", value)\n'
              'to consume {a point:value}: @intrinsic("discard", value)\n'
              'observe 1.0\n'
              'consume (@intrinsic("construct", a point, 1))\n'
              '@intrinsic("discard", @intrinsic("variadic call", "libc", "printf", '
              '@intrinsic("type", "int", 32), 1, "%lld\\n", '
              '@intrinsic("size of", @intrinsic("type of", @intrinsic("construct", a point, 1)))))\n')
    provider.write_text(source)
    initial = build(entry=provider)
    provider.write_text(source.replace('"type", "int"', '"type", "float"'))
    changed = build(entry=provider)
    assert changed[0]["constraint_probes_run"] >= 1, \
        "changed class field type was not re-evaluated"
    assert changed[2] != initial[2], "class field type edit did not change its runtime layout"
    assert changed[1:] == build(False, entry=provider)[1:]


def check_equivalent_recursive_layouts(root, build):
    main = root / "equivalent_recursive_layouts.dl"
    source = '''[a|] pointer to pointee means: @intrinsic("add pointer depth", pointee)
integer means: @intrinsic("type", "int")
function set variable to value:
    replacement:
        @intrinsic("store", variable, value)
flex section if condition:
    replacement:
        @intrinsic("if", condition)

class:
    patterns:
        an entry
    members:
        next as a pointer to an entry
        document as a pointer to metadata
class:
    patterns:
        a cursor
    members:
        first as a pointer to an entry

class:
    patterns:
        metadata
    members:
        owner as a pointer to a cursor

to get fresh metadata:
    @intrinsic("discard", @intrinsic("call", "libc", "rand", integer))
    @intrinsic("return", @intrinsic("construct", a pointer to metadata))

to touch: @intrinsic("discard", 1)

to scan {a pointer to a cursor:state} with {integer:depth}:
    if @intrinsic("not equal", depth, 0):
        set item to @intrinsic("construct", an entry)
        set item's document to fresh metadata
        set item's next to state's first
        set state's first to @intrinsic("construct", a pointer to @intrinsic("type of", item))
        scan state with @intrinsic("subtract", depth, 1)

set position to @intrinsic("construct", a cursor)
touch
scan @intrinsic("address of", position) with 2
'''
    main.write_text(source)
    cold = build(entry=main)
    assert cold[1:] == build(False, entry=main)[1:]
    main.write_text(source.replace('"discard", 1', '"discard", 2'))
    edited = build(entry=main)
    assert edited[0]["constraint_values_reused"] > 0
    assert edited[0]["inference_instantiations_reused"] > 0
    assert edited[1:] == build(False, entry=main)[1:]


def check_class_symbol_identity(root, build):
    main = root / "class_symbol_identity.dl"
    source = '''integer means: @intrinsic("type", "int", 32)
class:
    patterns:
        box
    members:
        value

to get inspect box value:
    @intrinsic("return", @intrinsic("less than", @intrinsic("cast", value's value, @intrinsic("type", "float", 64)), 2.0))

to get inspect collection values:
    @intrinsic("store", item, @intrinsic("extract element", values, 0))
    @intrinsic("return", inspect box item)

@intrinsic("store", first, @intrinsic("construct", box, @intrinsic("call", "libc", "abs", integer, 1)))
@intrinsic("store", second, @intrinsic("construct", box, @intrinsic("cast", @intrinsic("call", "libc", "abs", integer, 3), @intrinsic("type", "float", 64))))
'''
    for aggregate in (False, True):
        prepared = source
        action = "inspect box"
        if aggregate:
            prepared += '@intrinsic("store", {first group}, [first])\n'
            prepared += '@intrinsic("store", {second group}, [second])\n'
            action = "inspect collection"
        names = ["{first group}", "{second group}"] if aggregate else ["first", "second"]
        calls = [
            f'@intrinsic("discard", @intrinsic("call", "libc", "putchar", integer, '
            f'@intrinsic("add", 48, @intrinsic("cast", {action} {name}, integer))))\n'
            for name in names
        ]
        for flags in ((), ("-O2",)):
            main.write_text(prepared + "".join(calls))
            assert build(entry=main, flags=flags)[2] == b"10"
            main.write_text(prepared + "".join(reversed(calls)))
            changed = build(entry=main, flags=flags)
            assert changed[0]["inference_instantiations_reused"] > 0, "class call edit did not reuse inference"
            assert changed[1:] == build(False, entry=main, flags=flags)[1:], "reordered class layouts reused another instantiation's function"
            assert changed[2] == b"01"
