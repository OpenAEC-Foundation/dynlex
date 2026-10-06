#!/usr/bin/env python3
from __future__ import annotations

import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from find_vulkan_icd import find_vulkan_icd, vulkan_driver_environment


if len(sys.argv) != 3:
    raise SystemExit(f"Usage: {Path(sys.argv[0]).name} <project-directory> <compiler>")

project = Path(sys.argv[1]).resolve()
compiler = Path(sys.argv[2]).resolve()

subprocess.run(
    [
        sys.executable,
        str(project / "scripts/generate_builtin_shaders.py"),
        "--check",
    ],
    check=True,
)
with tempfile.TemporaryDirectory(prefix="dynlex-vulkan-runtime-") as temporary_directory:
    temporary = Path(temporary_directory)
    executable = temporary / "graphics-runtime.out"
    subprocess.run(
        [str(compiler), str(project / "tests/runtime/graphics_runtime_vulkan.dl"), "-o", str(executable)],
        cwd=project,
        check=True,
    )
    vertex_shader = temporary / "passthrough.spv"
    fragment_shader = temporary / "plasma.spv"
    subprocess.run(
        [
            str(compiler),
            str(project / "tests/games/passthrough_vertex.dl"),
            "--emit-spirv",
            "--shader-stage=vertex",
            "-o",
            str(vertex_shader),
        ],
        cwd=project,
        check=True,
    )
    subprocess.run(
        [
            str(compiler),
            str(project / "tests/games/plasma_shader.dl"),
            "--emit-spirv",
            "--shader-stage=fragment",
            "-o",
            str(fragment_shader),
        ],
        cwd=project,
        check=True,
    )

    command = [str(executable)]
    environment = os.environ.copy()
    try:
        vulkan_icd = find_vulkan_icd()
    except RuntimeError as error:
        raise SystemExit(f"The Vulkan runtime test requires a Vulkan driver: {error}") from error
    environment = vulkan_driver_environment(vulkan_icd, environment)
    if sys.platform.startswith("linux"):
        xvfb_run = shutil.which("xvfb-run")
        if xvfb_run is None:
            raise SystemExit("The Vulkan runtime test requires xvfb-run on Linux")
        command = [
            xvfb_run,
            "--auto-servernum",
            "--server-args=-screen 0 128x64x24",
            *command,
        ]
    elif sys.platform != "darwin":
        raise SystemExit(f"The Vulkan runtime test does not support {sys.platform}")
    subprocess.run(
        [*command, "success", str(vertex_shader), str(fragment_shader)],
        cwd=project, env=environment, check=True,
    )
    failures = {
        "invalid-window-width": "Graphics windows require positive dimensions",
        "invalid-fullscreen": "Graphics windows require positive dimensions",
        "negative-sleep": "A sleep duration cannot be negative",
        "invalid-mesh-count": "A graphics mesh requires a positive multiple of three vertices",
        "invalid-mesh-range": "A mesh draw range must contain complete triangles within the uploaded vertex array",
        "negative-mesh-range": "A mesh draw range must contain complete triangles within the uploaded vertex array",
        "release-active-mesh": "Releasing a mesh requires an idle graphics frame",
        "release-active-texture": "A graphics texture cannot be released while its triangle stream is active",
        "release-active-pipeline": "A graphics pipeline cannot be released during an active frame",
    }
    for scenario, expected_error in failures.items():
        result = subprocess.run(
            [*command, scenario, str(vertex_shader), str(fragment_shader)],
            cwd=project, env=environment, capture_output=True, text=True,
        )
        if result.returncode == 0 or expected_error not in result.stdout:
            raise RuntimeError(
                f"{scenario}: expected rejection containing {expected_error!r}; "
                f"exit={result.returncode}, stdout={result.stdout!r}, stderr={result.stderr!r}"
            )
