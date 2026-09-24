#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Emit the f64 planar-surface shader used by the GPU geometry probe."""

from __future__ import annotations

import argparse
from pathlib import Path
import struct


def string_words(value: str) -> list[int]:
    data = value.encode("utf-8") + b"\0"
    data += b"\0" * (-len(data) % 4)
    return list(struct.unpack(f"<{len(data) // 4}I", data))


words = [0x07230203, 0x00010300, 0, 0, 0]
next_identifier = 1


def identifier() -> int:
    global next_identifier
    value = next_identifier
    next_identifier += 1
    return value


def emit(opcode: int, *operands: int) -> None:
    words.extend([(len(operands) + 1) << 16 | opcode, *operands])


void = identifier()
function = identifier()
u32 = identifier()
uvec3 = identifier()
f64 = identifier()
array = identifier()
block = identifier()
input_pointer = identifier()
block_pointer = identifier()
value_pointer = identifier()
global_invocation = identifier()
storage = identifier()
main_function = identifier()
entry_label = identifier()

emit(17, 1)  # Shader
emit(17, 10)  # Float64
emit(14, 0, 1)  # Logical, GLSL450
emit(15, 5, main_function, *string_words("main"), global_invocation, storage)
emit(16, main_function, 17, 64, 1, 1)  # LocalSize
emit(71, global_invocation, 11, 28)  # GlobalInvocationId
emit(71, array, 6, 8)  # ArrayStride
emit(72, block, 0, 35, 0)  # member Offset
emit(71, block, 2)  # Block
emit(71, storage, 34, 0)  # DescriptorSet
emit(71, storage, 33, 0)  # Binding

emit(19, void)
emit(33, function, void)
emit(21, u32, 32, 0)
emit(23, uvec3, u32, 3)
emit(22, f64, 64)
emit(29, array, f64)
emit(30, block, array)
emit(32, input_pointer, 1, uvec3)
emit(32, block_pointer, 12, block)
emit(32, value_pointer, 12, f64)

constants = {}
for value in (0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 137):
    constant = identifier()
    constants[value] = constant
    emit(43, u32, constant, value)

emit(59, input_pointer, global_invocation, 1)
emit(59, block_pointer, storage, 12)
emit(54, void, main_function, 0, function)
emit(248, entry_label)


def integer_operation(opcode: int, left: int, right: int) -> int:
    result = identifier()
    emit(opcode, u32, result, left, right)
    return result


def float_operation(opcode: int, left: int, right: int) -> int:
    result = identifier()
    emit(opcode, f64, result, left, right)
    return result


def load_at(index: int) -> int:
    address = identifier()
    result = identifier()
    emit(65, value_pointer, address, storage, constants[0], index)
    emit(61, f64, result, address)
    return result


def store_at(index: int, value: int) -> None:
    address = identifier()
    emit(65, value_pointer, address, storage, constants[0], index)
    emit(62, address, value)


invocation = identifier()
sample = identifier()
emit(61, uvec3, invocation, global_invocation)
emit(81, u32, sample, invocation, 0)

# Buffer layout: origin[3], U[3], V[3], UV[64][2], XYZ[64][3].
coefficients = [load_at(constants[index]) for index in range(9)]
input_start = integer_operation(128, integer_operation(132, sample, constants[2]), constants[9])
u = load_at(input_start)
v = load_at(integer_operation(128, input_start, constants[1]))
output_start = integer_operation(128, integer_operation(132, sample, constants[3]), constants[137])

for axis in range(3):
    along_u = float_operation(133, u, coefficients[3 + axis])
    along_v = float_operation(133, v, coefficients[6 + axis])
    point = float_operation(129, float_operation(129, coefficients[axis], along_u), along_v)
    output_index = integer_operation(128, output_start, constants[axis])
    store_at(output_index, point)

emit(253)
emit(56)
words[3] = next_identifier


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(struct.pack(f"<{len(words)}I", *words))


if __name__ == "__main__":
    main()
