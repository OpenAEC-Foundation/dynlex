#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Measure private-byte growth across repeated success/refusal cleanup on Windows."""
from __future__ import annotations

import argparse
import ctypes
from pathlib import Path
import queue
import subprocess
import sys
import tempfile
import threading

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / "tests/cadkernel"))
import verify as fixtures


class ProcessMemoryCountersEx(ctypes.Structure):
    _fields_ = [
        ("cb", ctypes.c_ulong), ("page_fault_count", ctypes.c_ulong),
        ("peak_working_set_size", ctypes.c_size_t), ("working_set_size", ctypes.c_size_t),
        ("quota_peak_paged_pool_usage", ctypes.c_size_t),
        ("quota_paged_pool_usage", ctypes.c_size_t),
        ("quota_peak_nonpaged_pool_usage", ctypes.c_size_t),
        ("quota_nonpaged_pool_usage", ctypes.c_size_t),
        ("pagefile_usage", ctypes.c_size_t), ("peak_pagefile_usage", ctypes.c_size_t),
        ("private_usage", ctypes.c_size_t),
    ]


def private_bytes(pid: int) -> int:
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    psapi = ctypes.WinDLL("psapi", use_last_error=True)
    kernel.OpenProcess.argtypes = [ctypes.c_ulong, ctypes.c_int, ctypes.c_ulong]
    kernel.OpenProcess.restype = ctypes.c_void_p
    kernel.CloseHandle.argtypes = [ctypes.c_void_p]
    psapi.GetProcessMemoryInfo.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_ulong]
    handle = kernel.OpenProcess(0x0400 | 0x0010, 0, pid)
    if not handle:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        counters = ProcessMemoryCountersEx()
        counters.cb = ctypes.sizeof(counters)
        if not psapi.GetProcessMemoryInfo(handle, ctypes.byref(counters), counters.cb):
            raise ctypes.WinError(ctypes.get_last_error())
        return counters.private_usage
    finally:
        kernel.CloseHandle(handle)


def checkpoint(process: subprocess.Popen[str], mode: str) -> None:
    assert process.stdout
    received: queue.Queue[str] = queue.Queue(maxsize=1)
    threading.Thread(target=lambda: received.put(process.stdout.readline()), daemon=True).start()
    try:
        line = received.get(timeout=30)
    except queue.Empty as error:
        raise AssertionError(f"{mode}: checkpoint timed out") from error
    if line.strip() != "checkpoint":
        raise AssertionError(f"{mode}: missing checkpoint; received {line!r}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, default=ROOT / "build/dynlex.exe")
    args = parser.parse_args()
    if sys.platform != "win32":
        print("SKIP: Windows private-byte measurement")
        return
    with tempfile.TemporaryDirectory(prefix="cad-path-ownership-") as temporary:
        for optimization in ("O0", "O2"):
            executable = Path(temporary) / f"ownership-{optimization}.exe"
            status, output, _ = fixtures.run_process(
                [str(args.compiler), str(HERE / "ownership_stress.dl"),
                 f"-{optimization}", "-o", str(executable)], cwd=ROOT, timeout=180,
            )
            if status or output:
                raise AssertionError(f"{optimization}: compile exit {status}: {output}")
            with fixtures.unattended_child_processes(), subprocess.Popen(
                [str(executable)], cwd=ROOT, text=True, stdin=subprocess.PIPE,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                creationflags=subprocess.CREATE_NO_WINDOW | subprocess.CREATE_NEW_PROCESS_GROUP,
            ) as process:
                assert process.stdin
                try:
                    checkpoint(process, optimization)
                    first = private_bytes(process.pid)
                    process.stdin.write("\n")
                    process.stdin.flush()
                    checkpoint(process, optimization)
                    last = private_bytes(process.pid)
                    process.stdin.write("\n")
                    process.stdin.flush()
                    _, stderr = process.communicate(timeout=30)
                    if process.returncode:
                        raise AssertionError(f"{optimization}: exit {process.returncode}: {stderr}")
                finally:
                    if process.poll() is None:
                        fixtures.stop_process_tree(process)
            growth = last - first
            print(f"{optimization}: warmup={first} final={last} growth={growth}", flush=True)
            if growth > 4 * 1024 * 1024:
                raise AssertionError(f"{optimization}: private bytes grew over 4 MiB after warmup")
    print("PASS: bounded private-byte growth after 200000 cleaned calls per path at O0/O2")


if __name__ == "__main__":
    main()
