#!/usr/bin/env python3
"""Check caller-owned float normal distributions against a platform-independent algorithm reference."""

from __future__ import annotations

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from native_abi import C_SYMBOL_MACROS, abi_symbols, one_symbol


def fail(message: str) -> None:
    raise RuntimeError(message)


def run(command: list[str], cwd: Path) -> None:
    result = subprocess.run(command, cwd=cwd, text=True, capture_output=True, check=False)
    if result.returncode:
        fail(f"command failed ({result.returncode}): {' '.join(command)}\n{result.stdout}{result.stderr}")


def main() -> int:
    if len(sys.argv) != 2:
        fail("usage: test_normal_distribution.py <compiler>")
    root = Path(__file__).resolve().parent.parent
    compiler = Path(sys.argv[1]).resolve()
    cxx = shutil.which("c++")
    if not compiler.is_file() or cxx is None:
        fail("normal-distribution native tests require the compiler and c++")

    with tempfile.TemporaryDirectory(prefix="dynlex-normal-native-") as temporary:
        output = Path(temporary)
        wrapper = output / "normal_distribution_abi.o"
        llvm_output = output / "normal_distribution_abi.ll"
        run([str(compiler), str(root / "tests/required/normal_distribution_native/abi.dl"),
             "--emit-llvm", "--no-main", "-O2", "-ffp-contract=off", "-o", str(llvm_output)], root)
        found = abi_symbols(llvm_output.read_text(encoding="utf-8"))
        selected = {
            "seed32": one_symbol(found, "abi_seed_normal_MT32_at_"),
            "seed64": one_symbol(found, "abi_seed_normal_MT64_at_"),
            "next32": one_symbol(found, "abi_next_normal_MT32_at_"),
            "next64": one_symbol(found, "abi_next_normal_MT64_at_"),
            "reset": one_symbol(found, "abi_reset_normal_at_"),
            "raw32": one_symbol(found, "abi_next_raw_MT32_at_"),
            "raw64": one_symbol(found, "abi_next_raw_MT64_at_"),
        }
        source = output / "corpus.cpp"
        source.write_text(f'''{C_SYMBOL_MACROS}
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <random>
#include "random_reference.h"
#include <type_traits>

struct Engine32 {{ std::uint32_t state[624]; std::uint32_t index; }};
struct Engine64 {{ std::uint64_t state[312]; std::uint64_t index; }};
struct NormalState {{ float cached; bool available; }};
static_assert(sizeof(Engine32) == 2500 && sizeof(Engine64) == 2504);
static_assert(sizeof(NormalState) == 8 && offsetof(NormalState, available) == 4);
extern "C" void seed32(void*, std::uint32_t) __asm__(DYNLEX_SYMBOL("{selected['seed32']}"));
extern "C" void seed64(void*, std::uint64_t) __asm__(DYNLEX_SYMBOL("{selected['seed64']}"));
extern "C" float next32(void*, void*, float, float) __asm__(DYNLEX_SYMBOL("{selected['next32']}"));
extern "C" float next64(void*, void*, float, float) __asm__(DYNLEX_SYMBOL("{selected['next64']}"));
extern "C" void reset(void*) __asm__(DYNLEX_SYMBOL("{selected['reset']}"));
extern "C" std::uint32_t raw32(void*) __asm__(DYNLEX_SYMBOL("{selected['raw32']}"));
extern "C" std::uint64_t raw64(void*) __asm__(DYNLEX_SYMBOL("{selected['raw64']}"));

static void require(bool condition, const char* message) {{
    if (!condition) {{ std::cerr << message << "\\n"; std::abort(); }}
}}
static void equal(float actual, float expected, const char* what, int seed, int draw) {{
    if (std::bit_cast<std::uint32_t>(actual) != std::bit_cast<std::uint32_t>(expected)) {{
        std::cerr << what << " seed=" << seed << " draw=" << draw << " actual=0x"
                  << std::hex << std::bit_cast<std::uint32_t>(actual) << " expected=0x"
                  << std::bit_cast<std::uint32_t>(expected) << std::dec << "\\n";
        std::abort();
    }}
}}

static std::uint32_t undoRight32(std::uint32_t value, int shift) {{
    std::uint32_t result = value;
    for (int iteration = 0; iteration < 8; ++iteration) result = value ^ (result >> shift);
    return result;
}}
static std::uint32_t undoLeft32(std::uint32_t value, int shift, std::uint32_t mask) {{
    std::uint32_t result = value;
    for (int iteration = 0; iteration < 8; ++iteration) result = value ^ ((result << shift) & mask);
    return result;
}}
static std::uint32_t untemper32(std::uint32_t value) {{
    value = undoRight32(value, 18);
    value = undoLeft32(value, 15, UINT32_C(0xefc60000));
    value = undoLeft32(value, 7, UINT32_C(0x9d2c5680));
    return undoRight32(value, 11);
}}
static std::uint64_t undoRight64(std::uint64_t value, int shift, std::uint64_t mask) {{
    std::uint64_t result = value;
    for (int iteration = 0; iteration < 8; ++iteration) result = value ^ ((result >> shift) & mask);
    return result;
}}
static std::uint64_t undoLeft64(std::uint64_t value, int shift, std::uint64_t mask) {{
    std::uint64_t result = value;
    for (int iteration = 0; iteration < 8; ++iteration) result = value ^ ((result << shift) & mask);
    return result;
}}
static std::uint64_t untemper64(std::uint64_t value) {{
    value = undoRight64(value, 43, UINT64_MAX);
    value = undoLeft64(value, 37, UINT64_C(0xfff7eee000000000));
    value = undoLeft64(value, 17, UINT64_C(0x71d67fffeda60000));
    return undoRight64(value, 29, UINT64_C(0x5555555555555555));
}}

template<class Engine, class Seed, class SeedFn, class RawFn, class NextFn>
static void corpus(SeedFn seedFn, RawFn rawFn, NextFn nextFn, const Seed* seeds, int seedCount,
                   const char* label) {{
    for (int seedNumber = 0; seedNumber < seedCount; ++seedNumber) {{
        const Seed seed = seeds[seedNumber];
        Engine actualEngine{{}}; NormalState actualState{{}};
        seedFn(&actualEngine, seed);
        std::conditional_t<std::is_same_v<Seed, std::uint32_t>, std::mt19937, std::mt19937_64>
            expected(seed);
        random_reference::NormalDistribution distribution;
        for (int draw = 0; draw < 10000; ++draw) {{
            const float mean = (draw % 7 == 0) ? -3.25f : ((draw % 3) * 0.375f);
            const float deviation = (draw % 11 == 0) ? 2.5f : (0.25f + (draw % 5) * 0.5f);
            const float expectedValue = distribution(expected, mean, deviation);
            const float actualValue = nextFn(&actualEngine, &actualState, mean, deviation);
            equal(actualValue, expectedValue, label, seedNumber, draw);
        }}
        const auto expectedRaw = expected();
        require(rawFn(&actualEngine) == expectedRaw, "raw engine advancement mismatch");

        Engine first{{}}, second{{}}; NormalState firstState{{}}, secondState{{}};
        seedFn(&first, seed); seedFn(&second, seed);
        std::conditional_t<std::is_same_v<Seed, std::uint32_t>, std::mt19937, std::mt19937_64>
            firstExpected(seed), secondExpected(seed);
        random_reference::NormalDistribution firstDistribution;
        random_reference::NormalDistribution secondDistribution;
        equal(nextFn(&first, &firstState, 1.0f, 2.0f), firstDistribution(firstExpected, 1.0f, 2.0f),
              "independent first", seedNumber, 0);
        equal(nextFn(&second, &secondState, 1.0f, 2.0f), secondDistribution(secondExpected, 1.0f, 2.0f),
              "independent second", seedNumber, 0);
        reset(&firstState); firstDistribution.reset();
        equal(nextFn(&first, &firstState, -1.0f, 0.5f), firstDistribution(firstExpected, -1.0f, 0.5f),
              "reset", seedNumber, 0);
        const Seed reseeded = static_cast<Seed>(seed + static_cast<Seed>(1));
        seedFn(&first, reseeded); reset(&firstState); firstExpected.seed(reseeded);
        firstDistribution.reset();
        equal(nextFn(&first, &firstState, 2.0f, 0.25f), firstDistribution(firstExpected, 2.0f, 0.25f),
              "reseed", seedNumber, 0);

        Engine interleaved{{}}; NormalState interleavedState{{}};
        seedFn(&interleaved, seed);
        std::conditional_t<std::is_same_v<Seed, std::uint32_t>, std::mt19937, std::mt19937_64>
            interleavedExpected(seed);
        random_reference::NormalDistribution interleavedDistribution;
        for (int draw = 0; draw < 80; ++draw) {{
            if ((draw % 4) == 0) {{
                require(rawFn(&interleaved) == interleavedExpected(),
                        "interleaved raw engine advancement mismatch");
            }} else {{
                const float mean = (draw % 2 == 0) ? -0.75f : 1.25f;
                const float deviation = (draw % 3 == 0) ? 0.375f : 1.75f;
                equal(nextFn(&interleaved, &interleavedState, mean, deviation),
                      interleavedDistribution(interleavedExpected, mean, deviation), "interleaved normal", seedNumber, draw);
            }}
        }}
    }}
}}

struct Script32 {{ using result_type = std::uint32_t; const std::uint32_t* values; int count = 0; int position = 0;
    static constexpr result_type min() {{ return 0; }} static constexpr result_type max() {{ return UINT32_MAX; }}
    result_type operator()() {{ require(position < count, "scripted 32-bit engine overdraw"); return values[position++]; }} }};
struct Script64 {{ using result_type = std::uint64_t; const std::uint64_t* values; int count = 0; int position = 0;
    static constexpr result_type min() {{ return 0; }} static constexpr result_type max() {{ return UINT64_MAX; }}
    result_type operator()() {{ require(position < count, "scripted 64-bit engine overdraw"); return values[position++]; }} }};

static void scriptedCorpus() {{
    const std::uint32_t sequence32[] = {{0, 0, UINT32_MAX, UINT32_MAX, UINT32_C(0x80000000), UINT32_C(0x80000000), 1, UINT32_C(0x80000000)}};
    const std::uint64_t sequence64[] = {{0, 0, UINT64_MAX, UINT64_MAX, UINT64_C(0x8000000000000000), UINT64_C(0x8000000000000000), 1, UINT64_C(0x8000000000000000)}};
    Script32 expected32{{sequence32, 8}}; random_reference::NormalDistribution distribution32;
    const float firstExpected32 = distribution32(expected32);
    Engine32 actual32{{}}; NormalState state32{{}}; actual32.index = 0;
    for (int index = 0; index < 8; ++index) actual32.state[index] = untemper32(sequence32[index]);
    equal(next32(&actual32, &state32, 0.0f, 1.0f), firstExpected32, "scripted MT32", 0, 0);
    equal(next32(&actual32, &state32, 4.0f, 3.0f),
          distribution32(expected32) * 3.0f + 4.0f, "scripted MT32 cached", 0, 1);
    require(actual32.index == 8, "cached MT32 result consumed an engine draw");
    require(expected32.position == 8, "scripted MT32 expected draw count mismatch");

    Script64 expected64{{sequence64, 8}}; random_reference::NormalDistribution distribution64;
    const float firstExpected64 = distribution64(expected64);
    Engine64 actual64{{}}; NormalState state64{{}}; actual64.index = 0;
    for (int index = 0; index < 8; ++index) actual64.state[index] = untemper64(sequence64[index]);
    equal(next64(&actual64, &state64, 0.0f, 1.0f), firstExpected64, "scripted MT64", 0, 0);
    equal(next64(&actual64, &state64, 4.0f, 3.0f),
          distribution64(expected64) * 3.0f + 4.0f, "scripted MT64 cached", 0, 1);
    require(actual64.index == 8, "cached MT64 result consumed an engine draw");
    require(expected64.position == 8, "scripted MT64 expected draw count mismatch");
}}

int main() {{
    const std::uint32_t seeds32[] = {{0, 1, 5489, UINT32_C(1) << 31, UINT32_MAX}};
    const std::uint64_t seeds64[] = {{0, 1, 5489, UINT64_C(1) << 63, UINT64_MAX}};
    corpus<Engine32>(seed32, raw32, next32, seeds32, 5, "MT32 normal");
    corpus<Engine64>(seed64, raw64, next64, seeds64, 5, "MT64 normal");
    scriptedCorpus();
}}
''', encoding="utf-8")
        for optimization in ("-O0", "-O2", "-O3"):
            run([str(compiler), str(root / "tests/required/normal_distribution_native/abi.dl"),
                 "--emit-object", "--no-main", optimization, "-ffp-contract=off", "-o", str(wrapper)], root)
            llvm_output = output / f"normal_distribution{optimization}.ll"
            run([str(compiler), str(root / "tests/required/normal_distribution_native/abi.dl"),
                 "--emit-llvm", "--no-main", optimization, "-ffp-contract=off", "-o", str(llvm_output)], root)
            llvm_ir = llvm_output.read_text(encoding="utf-8")
            if "@llvm.log.f32" not in llvm_ir or "@llvm.sqrt.f32" not in llvm_ir:
                fail(f"{optimization} normal distribution did not emit f32 log/sqrt")
            if "@llvm.log.f64" in llvm_ir or "@llvm.sqrt.f64" in llvm_ir:
                fail(f"{optimization} normal distribution widened f32 math")
            executable = output / f"normal_distribution_native{optimization}"
            run([cxx, "-std=c++20", "-I", str(root / "tests/native"), optimization, "-ffp-contract=off", str(wrapper), str(source),
                 "-o", str(executable)], root)
            run([str(executable)], root)
    print("normal-distribution native ABI corpus passed at -O0, -O2, and -O3")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
