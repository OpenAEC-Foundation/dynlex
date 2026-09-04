#!/usr/bin/env python3
from __future__ import annotations

import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from find_lavapipe_icd import find_lavapipe_icd, lavapipe_driver_environment


if len(sys.argv) != 3:
    raise SystemExit(f"Usage: {Path(sys.argv[0]).name} <project-directory> <compiler>")

project = Path(sys.argv[1]).resolve()
compiler = Path(sys.argv[2]).resolve()
build = project / "build"

subprocess.run(
    [
        sys.executable,
        str(project / "src/runtime/shaders/generate_builtin_shaders.py"),
        "--check",
    ],
    check=True,
)
subprocess.run(
    ["cmake", "--build", str(build), "--target", "dynlex_graphics_runtime_test"],
    check=True,
)

executable = build / "dynlex_graphics_runtime_test"
with tempfile.TemporaryDirectory(prefix="dynlex-vulkan-runtime-") as temporary_directory:
    temporary = Path(temporary_directory)
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

    command = [str(executable), str(vertex_shader), str(fragment_shader)]
    environment = os.environ.copy()
    if sys.platform.startswith("linux"):
        xvfb_run = shutil.which("xvfb-run")
        if xvfb_run is None:
            raise SystemExit("The Vulkan runtime test requires xvfb-run on Linux")
        try:
            vulkan_icd = find_lavapipe_icd()
        except RuntimeError as error:
            raise SystemExit(f"The Vulkan runtime test requires Lavapipe: {error}") from error
        environment = lavapipe_driver_environment(vulkan_icd, environment)
        command = [
            xvfb_run,
            "--auto-servernum",
            "--server-args=-screen 0 128x64x24",
            *command,
        ]
    elif sys.platform != "darwin":
        raise SystemExit(f"The Vulkan runtime test does not support {sys.platform}")
    subprocess.run(command, cwd=project, env=environment, check=True)
