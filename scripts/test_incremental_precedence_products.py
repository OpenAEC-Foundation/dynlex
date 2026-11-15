"""The global precedence product can be reused without concrete replay keys."""


def check_precedence_product_reuse(root, build):
    main = root / "precedence_product_reuse.dl"
    source = ('function left plus right:\n'
              '    before: $ times $\n'
              '    replacement: @intrinsic("add", left, right)\n'
              'function left times right:\n'
              '    replacement: @intrinsic("multiply", left, right)\n'
              '@intrinsic("discard", 1)\n')
    main.write_text(source)
    build(entry=main)
    main.write_text(source.replace('"discard", 1', '"discard", 2'))
    edited = build(entry=main)
    assert edited[0]["precedence_match_keys_replayed"] == 0
    assert edited[0]["body_resolution_units_deferred"] == 0
    assert edited[0]["precedence_signature_reused"] >= 1, \
        "a global precedence product without reference replay keys was rebuilt"
    assert edited[1:] == build(False, entry=main)[1:]

    main.write_text(main.read_text().replace('before: $ times $', 'after: $ times $'))
    changed_rule = build(entry=main)
    assert changed_rule[0]["precedence_signature_reused"] == 0, \
        "changed precedence rules reused an old global fingerprint"
    assert changed_rule[1:] == build(False, entry=main)[1:]
