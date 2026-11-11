"""Compile-time result changes invalidate only their actual consumers."""


def check_value_query_dependencies(root, build):
    for nested in (False, True):
        main = root / f"value_queries_{nested}.dl"
        source = (
            'integer means: @intrinsic("type", "int")\n'
            'section if condition:\n    replacement: @intrinsic("if", condition)\n'
            'function [compute] {value}:\n    execute:\n'
            '        if @intrinsic("equal", value, 4):\n            @intrinsic("return", 1)\n'
            '        @intrinsic("return", 2)\n'
        )
        if nested:
            source += ('function [wrapped] {value}:\n    execute:\n'
                       '        @intrinsic("return", @intrinsic("add", compute value, 10))\n')
        name = 'wrapped' if nested else 'compute'
        for value in (4, 5):
            source += (f'to report {value}:\n'
                       '    @intrinsic("discard", @intrinsic("variadic call", "libc", "printf", integer, 1, '
                       f'"%d\\n", {name} {value}))\n')
        source += ('@intrinsic("discard", @intrinsic("variadic call", "libc", "printf", integer, 1, '
                   '"%d\\n", compute @intrinsic("command line argument count")))\n'
                   'report 4\nreport 5\n')
        main.write_text(source)
        assert build(entry=main)[2] == (b"2\n11\n12\n" if nested else b"2\n1\n2\n")
        first = source.replace('"return", 1)', '"return", 3)')
        main.write_text(first)
        changed = build(entry=main)
        assert changed[2] == (b"2\n13\n12\n" if nested else b"2\n3\n2\n")
        assert changed[1:] == build(False, entry=main)[1:]
        assert changed[0]["root_execution_reused"], "an unchanged runtime interface invalidated the entry point"
        assert changed[0]["preinferred_instantiations"] == (3 if nested else 2), \
            "the caller of the unchanged constant result was re-inferred"
        assert changed[0]["functions_ir_reused"] >= 1
        assert changed[0]["value_dependency_queries_reused"] >= 1
        main.write_text(first.replace('"return", 2)', '"return", 7)'))
        second = build(entry=main)
        assert second[2] == (b"7\n13\n17\n" if nested else b"7\n3\n7\n")
        assert second[1:] == build(False, entry=main)[1:]
        assert second[0]["root_execution_reused"], "retained callers lost their precise result dependencies"
        assert second[0]["preinferred_instantiations"] == (3 if nested else 2)
