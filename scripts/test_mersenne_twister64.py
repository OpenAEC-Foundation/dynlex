#!/usr/bin/env python3
"""Check the caller-owned MT19937-64 ABI against libstdc++."""

from __future__ import annotations

import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def fail(message: str) -> None:
    raise RuntimeError(message)


def run(command: list[str], cwd: Path) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(command, cwd=cwd, text=True, capture_output=True, check=False)
    if result.returncode != 0:
        fail(f"command failed ({result.returncode}): {' '.join(command)}\n{result.stdout}{result.stderr}")
    return result


def main() -> int:
    if len(sys.argv) != 2:
        fail("usage: test_mersenne_twister64.py <compiler>")
    root = Path(__file__).resolve().parent.parent
    compiler = Path(sys.argv[1]).resolve()
    if not compiler.is_file():
        fail(f"compiler not found: {compiler}")
    cxx = shutil.which("c++")
    if cxx is None:
        fail("a C++ compiler is required for the native ABI corpus")

    with tempfile.TemporaryDirectory(prefix="dynlex-mt64-native-") as temporary:
        output = Path(temporary)
        object_file = output / "mersenne_twister64_native-O2.o"
        run(
            [
                str(compiler),
                str(root / "tests/required/mersenne_twister64_native/abi.dl"),
                "--emit-object",
                "--no-main",
                "-O2",
                "-ffp-contract=off",
                "-o",
                str(object_file),
            ],
            root,
        )
        symbols = subprocess.run(
            ["nm", "-g", "--defined-only", str(object_file)],
            cwd=root,
            text=True,
            capture_output=True,
            check=False,
        )
        if symbols.returncode != 0:
            fail(f"nm failed ({symbols.returncode}): {symbols.stderr}")
        names = {}
        for line in symbols.stdout.splitlines():
            match = re.search(r"\b(abi_[A-Za-z0-9_]+)$", line)
            if match:
                names[match.group(1)] = match.group(1)
        required = {
            "seed": "abi_seed_Mersenne_Twister_64_at_",
            "next": "abi_next_64_bit_unsigned_integer_from_Mersenne_Twister_64_at_",
            "fraction64": "abi_next_64_bit_unit_fraction_from_Mersenne_Twister_64_at_",
            "fraction32": "abi_next_32_bit_unit_fraction_from_Mersenne_Twister_64_at_",
        }
        selected: dict[str, str] = {}
        for key, prefix in required.items():
            matches = [name for name in names if name.startswith(prefix)]
            if len(matches) != 1:
                fail(f"expected one {key} ABI symbol with prefix {prefix!r}, found {matches}")
            selected[key] = matches[0]

        source = output / "corpus.cpp"
        source.write_text(
            f'''#include <bit>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <random>

struct Engine {{ std::uint64_t state[312]; std::uint64_t index; }};
static_assert(sizeof(Engine) == 2504);
extern "C" void seed_fn(void*, std::uint64_t) __asm__("{selected['seed']}");
extern "C" std::uint64_t next_fn(void*) __asm__("{selected['next']}");
extern "C" double fraction64_fn(void*) __asm__("{selected['fraction64']}");
extern "C" float fraction32_fn(void*) __asm__("{selected['fraction32']}");

static std::uint64_t undoRight(std::uint64_t value, int shift, std::uint64_t mask) {{
    std::uint64_t result = value;
    for (int iteration = 0; iteration < 8; ++iteration) result = value ^ ((result >> shift) & mask);
    return result;
}}

static std::uint64_t undoLeft(std::uint64_t value, int shift, std::uint64_t mask) {{
    std::uint64_t result = value;
    for (int iteration = 0; iteration < 8; ++iteration) result = value ^ ((result << shift) & mask);
    return result;
}}

static std::uint64_t untemper(std::uint64_t value) {{
    value = undoRight(value, 43, UINT64_MAX);
    value = undoLeft(value, 37, UINT64_C(0xFFF7EEE000000000));
    value = undoLeft(value, 17, UINT64_C(0x71D67FFFEDA60000));
    return undoRight(value, 29, UINT64_C(0x5555555555555555));
}}

struct OneDraw {{
    using result_type = std::uint64_t;
    std::uint64_t value;
    bool consumed = false;
    static constexpr result_type min() {{ return 0; }}
    static constexpr result_type max() {{ return UINT64_MAX; }}
    result_type operator()() {{
        if (consumed) std::abort();
        consumed = true;
        return value;
    }}
}};

static int checkEndpoint(std::uint64_t output) {{
    Engine rawCheck{{}};
    rawCheck.index = 0;
    rawCheck.state[0] = untemper(output);
    if (next_fn(&rawCheck) != output) return 9;
    Engine actual{{}};
    actual.index = 0;
    actual.state[0] = untemper(output);
    OneDraw expected{{output}};
    const float actual32 = fraction32_fn(&actual);
    const float expected32 = std::generate_canonical<float, 24>(expected);
    if (std::bit_cast<std::uint32_t>(actual32) != std::bit_cast<std::uint32_t>(expected32)) {{
        std::cerr << "f32 endpoint mismatch " << output << " got "
                  << std::hex << std::bit_cast<std::uint32_t>(actual32) << " expected "
                  << std::bit_cast<std::uint32_t>(expected32) << std::dec << "\\n";
        return 6;
    }}

    actual.index = 0;
    actual.state[0] = untemper(output);
    expected = OneDraw{{output}};
    const double actual64 = fraction64_fn(&actual);
    const double expected64 = std::generate_canonical<double, 53>(expected);
    if (std::bit_cast<std::uint64_t>(actual64) != std::bit_cast<std::uint64_t>(expected64)) {{
        std::cerr << "f64 endpoint mismatch " << output << " got "
                  << std::hex << std::bit_cast<std::uint64_t>(actual64) << " expected "
                  << std::bit_cast<std::uint64_t>(expected64) << std::dec << "\\n";
        return 7;
    }}
    return 0;
}}

int main() {{
    const std::uint64_t seeds[] = {{0, 1, 5489, UINT64_C(1) << 63, UINT64_MAX}};
    for (std::uint64_t seed : seeds) {{
        Engine actual{{}};
        std::mt19937_64 expected(seed);
        seed_fn(&actual, seed);
        for (int draw = 0; draw < 10000; ++draw) {{
            if (next_fn(&actual) != expected()) return 1;
        }}

        Engine actualFractions{{}};
        std::mt19937_64 expectedFractions(seed);
        seed_fn(&actualFractions, seed);
        for (int draw = 0; draw < 10000; ++draw) {{
            const float actual32 = fraction32_fn(&actualFractions);
            const float expected32 = std::generate_canonical<float, 24>(expectedFractions);
            if (std::bit_cast<std::uint32_t>(actual32) != std::bit_cast<std::uint32_t>(expected32)) return 2;
            const double actual64 = fraction64_fn(&actualFractions);
            const double expected64 = std::generate_canonical<double, 53>(expectedFractions);
            if (std::bit_cast<std::uint64_t>(actual64) != std::bit_cast<std::uint64_t>(expected64)) return 3;
        }}

        Engine first{{}}, second{{}};
        seed_fn(&first, seed);
        seed_fn(&second, seed);
        if (next_fn(&first) != next_fn(&second)) return 4;
        std::mt19937_64 advancedExpected(seed);
        advancedExpected();
        for (int draw = 0; draw < 17; ++draw) {{
            if (next_fn(&first) != advancedExpected()) return 5;
        }}
        std::mt19937_64 resetExpected(seed);
        seed_fn(&second, seed);
        if (next_fn(&second) != resetExpected()) return 8;
    }}
    const std::uint64_t endpointValues[] = {{
        0,
        1,
        UINT64_C(1) << 32,
        UINT64_MAX / 2,
        UINT64_MAX - (UINT64_C(1) << 39) - 1,
        UINT64_MAX - (UINT64_C(1) << 39),
        UINT64_MAX - (UINT64_C(1) << 39) + 1,
        UINT64_MAX - 2049,
        UINT64_MAX - 2048,
        UINT64_MAX - 2047,
        UINT64_MAX,
    }};
    for (std::uint64_t output : endpointValues) {{
        const int result = checkEndpoint(output);
        if (result != 0) return result;
    }}
    return 0;
}}
''',
            encoding="utf-8",
        )
        for optimization in ("-O0", "-O2", "-O3"):
            optimized_object = output / f"mersenne_twister64_native{optimization}.o"
            run(
                [
                    str(compiler),
                    str(root / "tests/required/mersenne_twister64_native/abi.dl"),
                    "--emit-object",
                    "--no-main",
                    optimization,
                    "-ffp-contract=off",
                    "-o",
                    str(optimized_object),
                ],
                root,
            )
            executable = output / f"corpus{optimization}"
            run([cxx, "-std=c++20", "-O2", str(source), str(optimized_object), "-o", str(executable)], root)
            result = subprocess.run([str(executable)], cwd=root, check=False)
            if result.returncode != 0:
                fail(f"native MT19937-64 corpus {optimization} exited {result.returncode}")
    print("MT19937-64 native ABI corpus passed at -O0/-O2/-O3")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
