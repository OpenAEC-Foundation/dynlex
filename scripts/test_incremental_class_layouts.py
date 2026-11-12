"""Class-instantiation state restored before cross-process constraint reuse."""


def check_class_layout_restoration(root, build):
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

    provider = root / "class_field_function_provider.dl"
    source = source.replace('integer means: @intrinsic("type", "int")\n',
                            'function [integer]:\n    execute: @intrinsic("return", @intrinsic("type", "int"))\n')
    provider.write_text(source)
    initial = build(entry=provider)
    provider.write_text(source.replace('"type", "int"', '"type", "float"'))
    changed = build(entry=provider)
    assert changed[2] != initial[2], "a restored class request hid a changed field-type function"
    assert changed[1:] == build(False, entry=provider)[1:]
