#!/usr/bin/env python3
"""Check target restrictions and by-reference storage across function calls."""
from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path

from test_shader_intrinsic_targets import spirv_instructions


PROJECT = Path(__file__).resolve().parent.parent
ABI_SOURCE = '''to increment value:
    @intrinsic("store", amount, 1)
    @intrinsic("set subject", @intrinsic("add", value, amount))
    @intrinsic("store", value, @intrinsic("subject"))
to forward value:
    increment value
    @intrinsic("store", value, @intrinsic("add", value, 2))
function run:
    globals: number
    execute:
        @intrinsic("store", local, 5)
        forward local
        forward number
        @intrinsic("return", @intrinsic("add", local, number))
@intrinsic("store", number, 1)
@intrinsic("return", run)
'''
RECURSIVE_SOURCE = '''flex section when condition:
    replacement:
        @intrinsic("if", condition)
to advance value with depth:
    @intrinsic("store", temporary, value)
    when @intrinsic("greater than", depth, 0):
        advance temporary with @intrinsic("subtract", depth, 1)
    @intrinsic("store", value, @intrinsic("add", temporary, 1))
function run:
    globals: number
    execute:
        advance number with 2
@intrinsic("store", number, 1)
run
@intrinsic("return", number)
'''
EXTERNAL_CALLS = {
    "call": '@intrinsic("call", "libc", "puts", @intrinsic("type", "int"), "hello")',
    "variadic call": '@intrinsic("variadic call", "libc", "printf", @intrinsic("type", "int"), 1, "%s", "hello")',
}
RESOURCE_DECLARATION = '''an integer means: @intrinsic("type", "int")
class:
    patterns:
        a resource
    members:
        value as an integer
    retain:
        @intrinsic("discard", @intrinsic("lifecycle value"))
    release:
        @intrinsic("call", "libc", "abort", @intrinsic("type", "nothing"))
'''
TYPE_ONLY_SOURCES = {
    "size-only": '@intrinsic("discard", @intrinsic("size of", a resource))\n',
    "result-type-only": '''to get create resource:
    @intrinsic("return", @intrinsic("construct", a resource, 1))
@intrinsic("discard", @intrinsic("size of", @intrinsic("type of", create resource)))
''',
    "pointer-only": '@intrinsic("store", pointer, @intrinsic("cast", 0, @intrinsic("add pointer depth", a resource)))\n',
    "unused-replacement-argument": '''to get produce resource:
    @intrinsic("return", @intrinsic("construct", a resource, 1))
function ignore {a resource:value}:
    replacement:
        @intrinsic("discard", 1)
ignore produce resource
''',
}


def compile_source(compiler: Path, root: Path, name: str, source: str, flags: tuple[str, ...]):
    path = root / f"{name}.dl"
    output = root / f"{name}.out"
    path.write_text(source, encoding="utf-8")
    result = subprocess.run(
        [str(compiler), str(path), "-O0", *flags, "-o", str(output)],
        cwd=PROJECT, capture_output=True, text=True, timeout=15,
    )
    return result, output


def check_storage_signatures(path: Path) -> None:
    pointer_classes = {}
    function_parameter_types = {}
    function = None
    for opcode, operands in spirv_instructions(path):
        if opcode == 32:  # OpTypePointer
            pointer_classes[operands[0]] = operands[1]
        elif opcode == 54:  # OpFunction
            function = operands[1]
            function_parameter_types[function] = []
        elif opcode == 55:  # OpFunctionParameter
            function_parameter_types[function].append(operands[0])
    parameter_classes = {
        pointer_classes[type_id]
        for types in function_parameter_types.values() for type_id in types
        if type_id in pointer_classes
    }
    assert {6, 7} <= parameter_classes, f"missing Private/Function reference parameters: {parameter_classes}"


def main() -> None:
    compiler = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="dynlex-spirv-call-abi-") as directory:
        root = Path(directory)
        for intrinsic, call in EXTERNAL_CALLS.items():
            source = f'@intrinsic("discard", {call})\n'
            for stage in ("vertex", "fragment", "compute"):
                result, output = compile_source(
                    compiler, root, f"rejected-{intrinsic.replace(' ', '-')}-{stage}", source,
                    ("--emit-spirv", f"--shader-stage={stage}"),
                )
                assert result.returncode == 1, (result.returncode, result.stderr)
                expected = f"Intrinsic '{intrinsic}' is only available when emitting native CPU or WebAssembly code"
                assert expected in result.stderr, result.stderr
                assert not output.exists(), output
            for target in ("--emit-llvm", "--emit-wasm"):
                result, output = compile_source(
                    compiler, root, f"supported-{intrinsic.replace(' ', '-')}-{target[7:]}", source, (target,),
                )
                assert result.returncode == 0 and output.stat().st_size, result.stderr
        for name, source, expected in (("forwarding", ABI_SOURCE, 12), ("recursion", RECURSIVE_SOURCE, 4)):
            result, output = compile_source(compiler, root, name, source, ())
            assert result.returncode == 0, result.stderr
            actual = subprocess.run([str(output)], capture_output=True, text=True, timeout=5)
            assert actual.returncode == expected, (name, actual.returncode, actual.stderr)
            if name == "recursion":
                continue
            for stage in ("vertex", "fragment", "compute"):
                result, output = compile_source(
                    compiler, root, f"{name}-{stage}", source, ("--emit-spirv", f"--shader-stage={stage}"),
                )
                assert result.returncode == 0, (name, stage, result.stderr)
                check_storage_signatures(output)
                valid = subprocess.run(
                    ["spirv-val", "--target-env", "vulkan1.3", str(output)], capture_output=True, text=True, timeout=5,
                )
                assert valid.returncode == 0, valid.stderr
        for name, suffix in TYPE_ONLY_SOURCES.items():
            for stage in ("vertex", "fragment", "compute"):
                result, _ = compile_source(
                    compiler, root, name + stage, RESOURCE_DECLARATION + suffix,
                    ("--emit-spirv", f"--shader-stage={stage}"),
                )
                assert result.returncode == 0, (name, stage, result.stderr)
        runtime_uses = {
            "value": '@intrinsic("store", value, @intrinsic("construct", a resource, 1))\n',
            "nested-value": '''class:
    patterns:
        a container
    members:
        item as a resource
@intrinsic("store", value, @intrinsic("construct", a container))
''',
            "explicit-destruction": '''@intrinsic("store", pointer, @intrinsic("cast", 4096, @intrinsic("add pointer depth", a resource)))
@intrinsic("destroy at", pointer)
''',
            "captured-call": '''to get produce value:
    @intrinsic("store", local, @intrinsic("construct", a resource, 1))
    @intrinsic("return", 1)
function consume {arguments:values}:
    replacement:
        @intrinsic("store", {captured}, @intrinsic("extract element", values, 0))
        @intrinsic("discard", captured)
consume produce value
''',
        }
        for name, suffix in runtime_uses.items():
            result, _ = compile_source(
                compiler, root, name, RESOURCE_DECLARATION + suffix, ("--emit-spirv", "--shader-stage=fragment"),
            )
            assert result.returncode == 1 and "Intrinsic 'call'" in result.stderr, (name, result.stderr)
    print("SPIR-V external-call restrictions and local/global reference ABI checks passed")


if __name__ == "__main__":
    main()
