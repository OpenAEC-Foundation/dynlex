#!/usr/bin/env python3
from __future__ import annotations

import subprocess
import struct
import sys
import tempfile
from pathlib import Path


TARGETS_WITHOUT_SHADERS = (
    ("cpu", ()),
    ("wasm", ("--emit-wasm",)),
)
SHADER_INTRINSICS = {
    "shader input": "import lib/shader.dl\nset x to the fragment x coordinate\n",
    "shader uniform": '@intrinsic("discard", @intrinsic("shader uniform", "time"))\n',
    "shader output": '@intrinsic("shader output", 0.1, 0.2, 0.3, 1.0)\n',
}
INVALID_SPIRV_INPUTS = (
    (
        "non-literal-input",
        '@intrinsic("discard", @intrinsic("shader input", 1))\n',
        ("--emit-spirv", "--shader-stage=fragment"),
        "shader input requires a string literal name",
    ),
    (
        "unknown-input",
        '@intrinsic("discard", @intrinsic("shader input", "Noise"))\n',
        ("--emit-spirv", "--shader-stage=fragment"),
        "Unknown shader input: Noise",
    ),
    (
        "position-in-fragment",
        '@intrinsic("discard", @intrinsic("shader input", "Position"))\n',
        ("--emit-spirv", "--shader-stage=fragment"),
        "Shader input 'Position' is unavailable for this shader stage",
    ),
    (
        "fragcoord-in-vertex",
        '@intrinsic("discard", @intrinsic("shader input", "FragCoord"))\n',
        ("--emit-spirv", "--shader-stage=vertex"),
        "Shader input 'FragCoord' is unavailable for this shader stage",
    ),
)
FLEX_BRANCH_SHADER = """\
import lib/shader.dl

function bounded number:
    execute:
        set result to number
        clamp result between 0.0 and 1.0
        return result

set shade to bounded the shader time
set the fragment color to 0.0 shade 0.0 1.0
"""
NESTED_SELECTION_SHADER = """\
import lib/shader.dl

set shade to the fragment x coordinate
set altitude to the fragment y coordinate
if shade > 1.0:
    set shade to 1.0
else:
    if altitude > 2.0:
        set shade to 2.0

set the fragment color to shade shade shade 1.0
"""
ELSE_IF_SELECTION_SHADER = """\
import lib/shader.dl

set shade to the fragment x coordinate
if shade > 2.0:
    set shade to 2.0
else if shade > 1.0:
    set shade to 1.0

set the fragment color to shade shade shade 1.0
"""
BRANCHED_SHADER_OUTPUT = """\
import lib/shader.dl

set red to the fragment x coordinate
set green to the fragment y coordinate
set blue to the shader time
if red > 10.0:
    set red to green
set the fragment color to red green blue 1.0
"""
LOOP_WITH_NESTED_EXIT = """\
import lib/shader.dl

set shade to the fragment x coordinate
set step to 0
set hit to false
set travel to 0.0
while step < 52 and not hit:
    set sample to shade + travel
    if sample < 0.0:
        set hit to true
    else:
        set travel to travel + 1.0
        if travel > 44.0:
            set step to 52
    set step to step + 1

set the fragment color to shade shade shade 1.0
"""
REPEATED_UNIFORM_LOAD_SHADER = """\
function set var to val:
    replacement:
        @intrinsic("store", var, val)

function [the|] shader time:
    replacement:
        @intrinsic("shader uniform", "time")

set first to the shader time
set second to the shader time
@intrinsic("shader output", first, second, 0.0, 1.0)
"""
SPIRV_MAGIC = 0x07230203
OP_TYPE_INT = 21
OP_TYPE_POINTER = 32
OP_CONSTANT = 43
OP_SPEC_CONSTANT_OP = 52
OP_ACCESS_CHAIN = 65
OP_BITCAST = 124
OP_LOOP_MERGE = 246
OP_SELECTION_MERGE = 247
OP_BRANCH = 249
OP_BRANCH_CONDITIONAL = 250
STORAGE_CLASS_INPUT = 1
STORAGE_CLASS_OUTPUT = 3
STORAGE_CLASS_CROSS_WORKGROUP = 5


def compile_source(
    compiler: Path,
    directory: Path,
    case_name: str,
    source: str,
    arguments: tuple[str, ...],
) -> subprocess.CompletedProcess[str]:
    source_path = directory / f"{case_name}.dl"
    output_path = directory / f"{case_name}.out"
    source_path.write_text(source, encoding="utf-8")
    return subprocess.run(
        [str(compiler), str(source_path), *arguments, "-o", str(output_path)],
        text=True,
        capture_output=True,
        check=False,
    )


def spirv_instructions(path: Path) -> list[tuple[int, tuple[int, ...]]]:
    data = path.read_bytes()
    if len(data) < 20 or len(data) % 4 != 0:
        raise ValueError("SPIR-V output is not a complete word stream")
    words = struct.unpack(f"<{len(data) // 4}I", data)
    if words[0] != SPIRV_MAGIC:
        raise ValueError("SPIR-V output has the wrong magic number")
    instructions: list[tuple[int, tuple[int, ...]]] = []
    index = 5
    while index < len(words):
        instruction = words[index]
        word_count = instruction >> 16
        if word_count == 0 or index + word_count > len(words):
            raise ValueError("SPIR-V output contains an invalid instruction")
        instructions.append((
            instruction & 0xFFFF,
            words[index + 1 : index + word_count],
        ))
        index += word_count
    return instructions


def spirv_operands(path: Path, opcode: int) -> list[tuple[int, ...]]:
    return [operands for instruction_opcode, operands in spirv_instructions(path) if instruction_opcode == opcode]


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: test_shader_intrinsic_targets.py <compiler>", file=sys.stderr)
        return 2

    compiler = Path(sys.argv[1]).resolve()
    if not compiler.is_file():
        print(f"compiler not found: {compiler}", file=sys.stderr)
        return 2

    failures: list[str] = []
    with tempfile.TemporaryDirectory(prefix="dynlex-shader-targets-") as temporary_directory:
        temporary_path = Path(temporary_directory)

        for target_name, target_arguments in TARGETS_WITHOUT_SHADERS:
            for intrinsic_name, source in SHADER_INTRINSICS.items():
                result = compile_source(
                    compiler,
                    temporary_path,
                    f"{target_name}-{intrinsic_name.replace(' ', '-')}",
                    source,
                    target_arguments,
                )
                diagnostics = result.stdout + result.stderr
                expected = f"Shader operation '{intrinsic_name}' is only available when emitting SPIR-V"
                if result.returncode == 0:
                    failures.append(f"{target_name} accepted intrinsic '{intrinsic_name}'")
                elif result.returncode < 0:
                    failures.append(
                        f"{target_name} intrinsic '{intrinsic_name}' crashed with signal {-result.returncode}"
                    )
                elif expected not in diagnostics:
                    failures.append(
                        f"{target_name} intrinsic '{intrinsic_name}' produced the wrong diagnostic: {diagnostics.strip()}"
                    )

        for intrinsic_name, source in SHADER_INTRINSICS.items():
            result = compile_source(
                compiler,
                temporary_path,
                f"spirv-{intrinsic_name.replace(' ', '-')}",
                source,
                ("--emit-spirv", "--shader-stage=fragment"),
            )
            if result.returncode != 0:
                diagnostics = (result.stdout + result.stderr).strip()
                failures.append(f"SPIR-V rejected intrinsic '{intrinsic_name}': {diagnostics}")

        for case_name, source, arguments, expected in INVALID_SPIRV_INPUTS:
            result = compile_source(compiler, temporary_path, case_name, source, arguments)
            diagnostics = result.stdout + result.stderr
            if result.returncode == 0:
                failures.append(f"SPIR-V accepted invalid shader input case '{case_name}'")
            elif result.returncode < 0:
                failures.append(f"SPIR-V shader input case '{case_name}' crashed with signal {-result.returncode}")
            elif expected not in diagnostics:
                failures.append(
                    f"SPIR-V shader input case '{case_name}' produced the wrong diagnostic: {diagnostics.strip()}"
                )

        result = compile_source(
            compiler,
            temporary_path,
            "flex-branch-state",
            FLEX_BRANCH_SHADER,
            ("--emit-spirv", "--shader-stage=fragment", "-O0"),
        )
        if result.returncode != 0:
            diagnostics = (result.stdout + result.stderr).strip()
            failures.append(f"SPIR-V rejected the flex branch-state regression: {diagnostics}")
        else:
            output_path = temporary_path / "flex-branch-state.out"
            try:
                selection_count = len(spirv_operands(output_path, OP_SELECTION_MERGE))
            except ValueError as error:
                failures.append(str(error))
            else:
                if selection_count != 2:
                    failures.append(
                        f"SPIR-V flex clamp emitted {selection_count} conditional branches instead of 2"
                    )

        selection_cases = (
            ("nested-selection", NESTED_SELECTION_SHADER),
            ("else-if-selection", ELSE_IF_SELECTION_SHADER),
        )
        for selection_case_name, source in selection_cases:
            for optimization in ("-O0", "-O1", "-O2", "-O3"):
                case_name = f"{selection_case_name}-{optimization[1:]}"
                result = compile_source(
                    compiler,
                    temporary_path,
                    case_name,
                    source,
                    ("--emit-spirv", "--shader-stage=fragment", optimization),
                )
                if result.returncode != 0:
                    diagnostics = (result.stdout + result.stderr).strip()
                    failures.append(f"SPIR-V rejected {selection_case_name} at {optimization}: {diagnostics}")
                    continue

                try:
                    selections = spirv_operands(temporary_path / f"{case_name}.out", OP_SELECTION_MERGE)
                except ValueError as error:
                    failures.append(str(error))
                    continue

                merge_targets = [operands[0] for operands in selections]
                if len(selections) != 2:
                    failures.append(
                        f"SPIR-V {selection_case_name} at {optimization} emitted "
                        f"{len(selections)} selection merges instead of 2"
                    )
                elif len(set(merge_targets)) != len(merge_targets):
                    failures.append(
                        f"SPIR-V {selection_case_name} at {optimization} reused merge target %{merge_targets[0]}"
                    )

        for optimization in ("-O0", "-O1", "-O2", "-O3"):
            case_name = f"branched-shader-output-{optimization[1:]}"
            result = compile_source(
                compiler,
                temporary_path,
                case_name,
                BRANCHED_SHADER_OUTPUT,
                ("--emit-spirv", "--shader-stage=fragment", optimization),
            )
            if result.returncode != 0:
                diagnostics = (result.stdout + result.stderr).strip()
                failures.append(f"SPIR-V rejected branched shader output at {optimization}: {diagnostics}")
                continue

            try:
                specialization_operations = spirv_operands(
                    temporary_path / f"{case_name}.out",
                    OP_SPEC_CONSTANT_OP,
                )
                pointer_types = spirv_operands(
                    temporary_path / f"{case_name}.out",
                    OP_TYPE_POINTER,
                )
                bitcasts = spirv_operands(
                    temporary_path / f"{case_name}.out",
                    OP_BITCAST,
                )
            except ValueError as error:
                failures.append(str(error))
                continue

            if specialization_operations:
                failures.append(
                    f"SPIR-V branched shader output at {optimization} emitted "
                    f"{len(specialization_operations)} unexpected specialization constant operations"
                )
            pointer_storage_classes = [operands[1] for operands in pointer_types]
            pointer_type_ids = {operands[0] for operands in pointer_types}
            if STORAGE_CLASS_CROSS_WORKGROUP in pointer_storage_classes:
                failures.append(
                    f"SPIR-V branched shader output at {optimization} retained a CrossWorkgroup pointer"
                )
            if any(operands[0] in pointer_type_ids for operands in bitcasts):
                failures.append(
                    f"SPIR-V branched shader output at {optimization} emitted a logical pointer bitcast"
                )
            for expected_storage_class, label in (
                (STORAGE_CLASS_INPUT, "Input"),
                (STORAGE_CLASS_OUTPUT, "Output"),
            ):
                if expected_storage_class not in pointer_storage_classes:
                    failures.append(
                        f"SPIR-V branched shader output at {optimization} has no {label} pointer"
                    )

        result = compile_source(
            compiler,
            temporary_path,
            "loop-with-nested-exit",
            LOOP_WITH_NESTED_EXIT,
            ("--emit-spirv", "--shader-stage=fragment", "-O1"),
        )
        if result.returncode != 0:
            diagnostics = (result.stdout + result.stderr).strip()
            failures.append(f"SPIR-V rejected loop with nested exit: {diagnostics}")
        else:
            try:
                instructions = spirv_instructions(temporary_path / "loop-with-nested-exit.out")
            except ValueError as error:
                failures.append(str(error))
            else:
                loop_merge_indices = [
                    index
                    for index, (opcode, _operands) in enumerate(instructions)
                    if opcode == OP_LOOP_MERGE
                ]
                if len(loop_merge_indices) != 1:
                    failures.append(
                        f"SPIR-V loop with nested exit emitted {len(loop_merge_indices)} loop merges instead of 1"
                    )
                elif (
                    loop_merge_indices[0] + 1 >= len(instructions)
                    or instructions[loop_merge_indices[0] + 1][0] not in (OP_BRANCH, OP_BRANCH_CONDITIONAL)
                ):
                    failures.append("SPIR-V loop merge is not immediately followed by its branch")

        result = compile_source(
            compiler,
            temporary_path,
            "repeated-uniform-load",
            REPEATED_UNIFORM_LOAD_SHADER,
            ("--emit-spirv", "--shader-stage=fragment", "-O0"),
        )
        if result.returncode != 0:
            diagnostics = (result.stdout + result.stderr).strip()
            failures.append(f"SPIR-V rejected repeated uniform loads: {diagnostics}")
        else:
            output_path = temporary_path / "repeated-uniform-load.out"
            try:
                unsigned_integer_types = {
                    operands[0]
                    for operands in spirv_operands(output_path, OP_TYPE_INT)
                    if len(operands) == 3 and operands[1:] == (32, 0)
                }
                unsigned_zero_constants = {
                    operands[1]
                    for operands in spirv_operands(output_path, OP_CONSTANT)
                    if len(operands) == 3
                    and operands[0] in unsigned_integer_types
                    and operands[2] == 0
                }
                access_chains = spirv_operands(output_path, OP_ACCESS_CHAIN)
            except ValueError as error:
                failures.append(str(error))
            else:
                if not unsigned_zero_constants:
                    failures.append("SPIR-V uniform rewrite did not declare an unsigned 32-bit zero constant")
                if len(access_chains) != 2:
                    failures.append(
                        f"SPIR-V repeated uniform loads emitted {len(access_chains)} access chains instead of 2"
                    )
                else:
                    result_ids = [operands[1] for operands in access_chains]
                    if 0 in result_ids:
                        failures.append("SPIR-V uniform access chain used result ID 0")
                    elif len(set(result_ids)) != len(result_ids):
                        failures.append(
                            f"SPIR-V repeated uniform loads reused access-chain result ID %{result_ids[0]}"
                        )
                    for operands in access_chains:
                        if len(operands) != 4 or operands[3] not in unsigned_zero_constants:
                            failures.append("SPIR-V uniform access chain used an undeclared zero index")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
