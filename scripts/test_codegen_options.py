#!/usr/bin/env python3

from __future__ import annotations

import re
import subprocess
import sys
import tempfile
from pathlib import Path


SOURCE = """\
import lib/std.dl

exposed function blend {floating-point number:left} with {floating-point number:right}:
    execute:
        return left * right + left
"""

LOOP_VECTORIZATION_SOURCE = """\
import lib/std.dl

set count to @intrinsic("call", "libc", "time", a 64 bit integer, 0)
set allocation to @intrinsic("call", "libc", "malloc", a pointer, count * 8)
set numbers to allocation as a pointer to a 64 bit integer
set index to 0 as a 64 bit integer
loop while index < count:
    @intrinsic("store at", numbers + index, index)
    set index to index + 1
@intrinsic("call", "pipeline_test", "consume", nothing, numbers)
"""

SLP_VECTORIZATION_SOURCE = """\
import lib/std.dl

set seed to @intrinsic("call", "libc", "time", a 64 bit integer, 0)
set allocation to @intrinsic("call", "libc", "malloc", a pointer, 32)
set numbers to allocation as a pointer to a 64 bit integer
@intrinsic("store at", numbers + 0, seed + 0)
@intrinsic("store at", numbers + 1, seed + 1)
@intrinsic("store at", numbers + 2, seed + 2)
@intrinsic("store at", numbers + 3, seed + 3)
@intrinsic("call", "pipeline_test", "consume", nothing, numbers)
"""

LOOP_UNROLLING_SOURCE = """\
import lib/std.dl

set allocation to @intrinsic("call", "libc", "malloc", a pointer, 512)
set numbers to allocation as a pointer to a 64 bit integer
set index to 0 as a 64 bit integer
loop while index < 64:
    @intrinsic("store at", numbers + index, index)
    set index to index + 1
@intrinsic("call", "pipeline_test", "consume", nothing, numbers)
"""


def run_compiler(compiler: Path, repo_root: Path, source: Path, *arguments: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [str(compiler), str(source), *arguments],
        cwd=repo_root,
        text=True,
        capture_output=True,
        check=False,
    )


def compile_llvm(
    compiler: Path, repo_root: Path, source: Path, output_directory: Path, name: str, *arguments: str
) -> str:
    output = output_directory / f"{name}.ll"
    result = run_compiler(compiler, repo_root, source, "--emit-llvm", "-O0", *arguments, "-o", str(output))
    if result.returncode != 0:
        raise RuntimeError(result.stdout + result.stderr)
    return output.read_text(encoding="utf-8")


def floating_operation_line(llvm_ir: str, operation: str) -> str:
    match = re.search(rf"^.*\b{operation}\b.*$", llvm_ir, flags=re.MULTILINE)
    if match is None:
        raise RuntimeError(f"emitted LLVM IR has no {operation} instruction")
    return match.group(0)


def require_failure(result: subprocess.CompletedProcess[str], expected_message: str) -> None:
    output = result.stdout + result.stderr
    if result.returncode == 0:
        raise RuntimeError(f"compiler unexpectedly accepted invalid options: {result.args}")
    if expected_message not in output:
        raise RuntimeError(f"expected {expected_message!r} in compiler output:\n{output}")
    if "ignoring processor" in output or "ignoring feature" in output:
        raise RuntimeError(f"LLVM silently ignored an invalid target option:\n{output}")


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: test_codegen_options.py <compiler>", file=sys.stderr)
        return 2

    compiler = Path(sys.argv[1]).resolve()
    if not compiler.is_file():
        print(f"compiler not found: {compiler}", file=sys.stderr)
        return 2
    repo_root = Path(__file__).resolve().parent.parent

    try:
        help_result = subprocess.run(
            [str(compiler), "--help"], cwd=repo_root, text=True, capture_output=True, check=False
        )
        if help_result.returncode != 0:
            raise RuntimeError(help_result.stdout + help_result.stderr)
        for flag in (
            "-Ofast",
            "-ffast-math",
            "-ffp-contract=fast|off",
            "-march=native",
            "-mcpu=<cpu>",
            "-mtune=<cpu>",
            "-mattr=<features>",
            "-fvectorize",
            "-fslp-vectorize",
            "-funroll-loops",
        ):
            if flag not in help_result.stdout:
                raise RuntimeError(f"--help does not document {flag}")

        with tempfile.TemporaryDirectory(prefix="dynlex-codegen-options-") as temporary_directory:
            output_directory = Path(temporary_directory)
            source = output_directory / "options.dl"
            source.write_text(SOURCE, encoding="utf-8")

            default_ir = compile_llvm(compiler, repo_root, source, output_directory, "default")
            default_multiply = floating_operation_line(default_ir, "fmul")
            if " fast " in default_multiply or " contract " in default_multiply:
                raise RuntimeError(f"default floating-point behavior is not strict: {default_multiply}")
            if '"target-cpu"="generic"' not in default_ir:
                raise RuntimeError("native target CPU attributes are missing from emitted LLVM IR")

            fast_ir = compile_llvm(compiler, repo_root, source, output_directory, "fast", "-ffast-math")
            if " fast " not in floating_operation_line(fast_ir, "fmul"):
                raise RuntimeError("-ffast-math did not mark generated floating-point operations as fast")
            for attribute in (
                '"approx-func-fp-math"="true"',
                '"no-infs-fp-math"="true"',
                '"no-nans-fp-math"="true"',
                '"no-signed-zeros-fp-math"="true"',
                '"no-trapping-math"="true"',
                '"unsafe-fp-math"="true"',
            ):
                if attribute not in fast_ir:
                    raise RuntimeError(f"-ffast-math did not emit function attribute {attribute}")

            reset_ir = compile_llvm(
                compiler, repo_root, source, output_directory, "reset", "-ffast-math", "-fno-fast-math"
            )
            if " fast " in floating_operation_line(reset_ir, "fmul"):
                raise RuntimeError("-fno-fast-math did not restore strict floating-point operations")

            ofast_ir = compile_llvm(compiler, repo_root, source, output_directory, "ofast", "-Ofast", "-O0")
            if " fast " not in floating_operation_line(ofast_ir, "fmul"):
                raise RuntimeError("-Ofast did not enable fast floating-point operations")

            contract_ir = compile_llvm(
                compiler, repo_root, source, output_directory, "contract", "-ffp-contract=fast"
            )
            if " contract " not in floating_operation_line(contract_ir, "fmul"):
                raise RuntimeError("-ffp-contract=fast did not permit floating-point contraction")

            no_contract_ir = compile_llvm(
                compiler,
                repo_root,
                source,
                output_directory,
                "no-contract",
                "-ffast-math",
                "-ffp-contract=off",
            )
            no_contract_multiply = floating_operation_line(no_contract_ir, "fmul")
            if " contract " in no_contract_multiply or " reassoc " not in no_contract_multiply:
                raise RuntimeError(f"-ffp-contract=off changed more than contraction: {no_contract_multiply}")

            contract_then_strict_ir = compile_llvm(
                compiler,
                repo_root,
                source,
                output_directory,
                "contract-then-strict",
                "-ffp-contract=fast",
                "-fno-fast-math",
            )
            if " contract " not in floating_operation_line(contract_then_strict_ir, "fmul"):
                raise RuntimeError("-fno-fast-math erased an explicit preceding -ffp-contract=fast")

            strict_then_contract_ir = compile_llvm(
                compiler,
                repo_root,
                source,
                output_directory,
                "strict-then-contract",
                "-fno-fast-math",
                "-ffp-contract=fast",
            )
            if " contract " not in floating_operation_line(strict_then_contract_ir, "fmul"):
                raise RuntimeError("-ffp-contract=fast did not override preceding strict fast-math settings")

            native_ir = compile_llvm(compiler, repo_root, source, output_directory, "native", "-march=native")
            if '"target-cpu"="native"' in native_ir or '"target-cpu"=' not in native_ir:
                raise RuntimeError("-march=native was not resolved to concrete target information")
            cpu_match = re.search(r'"target-cpu"="([^"]+)"', native_ir)
            if cpu_match is None:
                raise RuntimeError("-march=native did not record a concrete host CPU")
            feature_match = re.search(r'"target-features"="([^"]+)"', native_ir)
            if feature_match is None and cpu_match.group(1) == "generic":
                raise RuntimeError("-march=native resolved to neither a concrete CPU nor concrete features")
            triple_match = re.search(r'^target triple = "([^"]+)"$', native_ir, flags=re.MULTILINE)
            if triple_match is None:
                raise RuntimeError("native LLVM IR did not record a target triple")
            architecture = triple_match.group(1).split("-", 1)[0]
            default_feature = {
                "aarch64": "+neon",
                "arm64": "+neon",
                "x86_64": "+sse2",
            }.get(architecture)
            if feature_match is None and default_feature is None:
                raise RuntimeError(f"no target-feature fixture is defined for {architecture}")
            detected_feature = feature_match.group(1).split(",")[0] if feature_match else default_feature
            if detected_feature is None:
                raise RuntimeError("native target feature selection is unavailable")
            overridden_feature = ("-" if detected_feature.startswith("+") else "+") + detected_feature[1:]
            overridden_ir = compile_llvm(
                compiler,
                repo_root,
                source,
                output_directory,
                "feature-override",
                "-march=native",
                f"-mattr={overridden_feature}",
            )
            overridden_match = re.search(r'"target-features"="([^"]+)"', overridden_ir)
            if overridden_match is None:
                raise RuntimeError("-mattr removed target feature information")
            matching_features = [
                feature
                for feature in overridden_match.group(1).split(",")
                if feature[1:] == overridden_feature[1:]
            ]
            if not matching_features or matching_features[-1] != overridden_feature:
                raise RuntimeError("-mattr did not override the detected host feature")

            concrete_architecture_ir = compile_llvm(
                compiler, repo_root, source, output_directory, "concrete-architecture", "-march=generic"
            )
            if '"target-cpu"="generic"' not in concrete_architecture_ir:
                raise RuntimeError("-march=<cpu> did not select the requested concrete LLVM CPU")

            last_cpu_option_ir = compile_llvm(
                compiler,
                repo_root,
                source,
                output_directory,
                "last-cpu-option",
                "-march=native",
                "-mcpu=generic",
            )
            if '"target-cpu"="generic"' not in last_cpu_option_ir:
                raise RuntimeError("the last -march/-mcpu option did not determine the target CPU")

            tuned_ir = compile_llvm(
                compiler, repo_root, source, output_directory, "tuned", "-mcpu=generic", "-mtune=native"
            )
            if '"target-cpu"="generic"' not in tuned_ir or '"tune-cpu"="native"' in tuned_ir:
                raise RuntimeError("-mcpu/-mtune did not emit resolved target attributes")
            if '"tune-cpu"=' not in tuned_ir:
                raise RuntimeError("-mtune did not emit a tuning CPU attribute")

            size_ir = compile_llvm(
                compiler,
                repo_root,
                source,
                output_directory,
                "size",
                "-Os",
                "-fno-vectorize",
                "-fno-slp-vectorize",
                "-fno-unroll-loops",
            )
            if " optsize " not in size_ir or " minsize " in size_ir:
                raise RuntimeError("-Os did not apply size-optimization function attributes")
            smallest_ir = compile_llvm(
                compiler,
                repo_root,
                source,
                output_directory,
                "smallest",
                "-Oz",
                "-fvectorize",
                "-fslp-vectorize",
                "-funroll-loops",
            )
            if " optsize " not in smallest_ir or " minsize " not in smallest_ir:
                raise RuntimeError("-Oz did not apply minimum-size function attributes")

            loop_source = output_directory / "loop-vectorization.dl"
            loop_source.write_text(LOOP_VECTORIZATION_SOURCE, encoding="utf-8")
            vectorized_loop_ir = compile_llvm(
                compiler,
                repo_root,
                loop_source,
                output_directory,
                "loop-vectorized",
                "-O3",
                "-march=native",
                "-fvectorize",
                "-fno-slp-vectorize",
            )
            scalar_loop_ir = compile_llvm(
                compiler,
                repo_root,
                loop_source,
                output_directory,
                "loop-scalar",
                "-O3",
                "-march=native",
                "-fno-vectorize",
                "-fno-slp-vectorize",
            )
            if not re.search(r"\b(?:load|store) <[0-9]+ x i64>", vectorized_loop_ir):
                raise RuntimeError("-fvectorize did not vectorize the test loop")
            if re.search(r"\b(?:load|store) <[0-9]+ x i64>", scalar_loop_ir):
                raise RuntimeError("-fno-vectorize did not keep the test loop scalar")

            slp_source = output_directory / "slp-vectorization.dl"
            slp_source.write_text(SLP_VECTORIZATION_SOURCE, encoding="utf-8")
            slp_ir = compile_llvm(
                compiler,
                repo_root,
                slp_source,
                output_directory,
                "slp-vectorized",
                "-O3",
                "-march=native",
                "-fno-vectorize",
                "-fslp-vectorize",
                "-fno-unroll-loops",
            )
            no_slp_ir = compile_llvm(
                compiler,
                repo_root,
                slp_source,
                output_directory,
                "slp-scalar",
                "-O3",
                "-march=native",
                "-fno-vectorize",
                "-fno-slp-vectorize",
                "-fno-unroll-loops",
            )
            if not re.search(r"\b(?:load|store) <[0-9]+ x i64>", slp_ir):
                raise RuntimeError("-fslp-vectorize did not vectorize the test basic block")
            if re.search(r"\b(?:load|store) <[0-9]+ x i64>", no_slp_ir):
                raise RuntimeError("-fno-slp-vectorize did not keep the test basic block scalar")

            unroll_source = output_directory / "loop-unrolling.dl"
            unroll_source.write_text(LOOP_UNROLLING_SOURCE, encoding="utf-8")
            unrolled_ir = compile_llvm(
                compiler,
                repo_root,
                unroll_source,
                output_directory,
                "loop-unrolled",
                "-O3",
                "-funroll-loops",
                "-fno-vectorize",
                "-fno-slp-vectorize",
            )
            not_unrolled_ir = compile_llvm(
                compiler,
                repo_root,
                unroll_source,
                output_directory,
                "loop-not-unrolled",
                "-O3",
                "-fno-unroll-loops",
                "-fno-vectorize",
                "-fno-slp-vectorize",
            )
            if unrolled_ir.count("store i64") <= not_unrolled_ir.count("store i64"):
                raise RuntimeError("-funroll-loops did not unroll the test loop")

            invalid_cpu = run_compiler(
                compiler,
                repo_root,
                source,
                "--emit-llvm",
                "-mcpu=dynlex-invalid-cpu",
                "-o",
                str(output_directory / "invalid-cpu.ll"),
            )
            require_failure(invalid_cpu, "unknown target CPU 'dynlex-invalid-cpu'")

            invalid_feature = run_compiler(
                compiler,
                repo_root,
                source,
                "--emit-llvm",
                "-mattr=+dynlex-invalid-feature",
                "-o",
                str(output_directory / "invalid-feature.ll"),
            )
            require_failure(invalid_feature, "unknown target feature '+dynlex-invalid-feature'")

            invalid_march = run_compiler(
                compiler,
                repo_root,
                source,
                "--emit-llvm",
                "-march=dynlex-invalid-architecture",
                "-o",
                str(output_directory / "invalid-march.ll"),
            )
            require_failure(invalid_march, "unknown target CPU 'dynlex-invalid-architecture'")

            non_native_target_options = run_compiler(
                compiler,
                repo_root,
                source,
                "--emit-wasm",
                "-mcpu=generic",
                "-o",
                str(output_directory / "invalid-target.wasm"),
            )
            require_failure(non_native_target_options, "CPU target options require native or LLVM output")
    except RuntimeError as error:
        print(error, file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
