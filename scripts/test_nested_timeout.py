#!/usr/bin/env python3
"""The suite watchdog must terminate children in nested timeout sessions."""

import ctypes
import os
from pathlib import Path
import signal
import shutil
import subprocess
import sys
import tempfile
import time


def running(pid):
    if os.name == "nt":
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel.OpenProcess.argtypes = [ctypes.c_ulong, ctypes.c_int, ctypes.c_ulong]
        kernel.OpenProcess.restype = ctypes.c_void_p
        kernel.GetExitCodeProcess.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_ulong)]
        kernel.CloseHandle.argtypes = [ctypes.c_void_p]
        handle = kernel.OpenProcess(0x1000, False, pid)
        if not handle:
            if ctypes.get_last_error() == 87:  # ERROR_INVALID_PARAMETER: PID no longer exists.
                return False
            raise ctypes.WinError(ctypes.get_last_error())
        try:
            status = ctypes.c_ulong()
            if not kernel.GetExitCodeProcess(handle, ctypes.byref(status)):
                raise ctypes.WinError(ctypes.get_last_error())
            return status.value == 259  # STILL_ACTIVE
        finally:
            kernel.CloseHandle(handle)
    status = subprocess.run(["ps", "-o", "stat=", "-p", str(pid)], capture_output=True, text=True, check=False)
    return bool(status.stdout.strip()) and not status.stdout.lstrip().startswith("Z")


def main():
    project = Path(__file__).resolve().parents[1]
    source = (project / "scripts" / "test.sh").read_text(encoding="utf-8")
    wrapper = "run_with_timeout() {" + source.split("run_with_timeout() {", 1)[1].split('\nif [[ "$run_only"', 1)[0]
    shell = os.environ.get("DYNLEX_TEST_BASH", "bash")
    with tempfile.TemporaryDirectory(prefix="dynlex-nested-timeout-") as temporary:
        root = Path(temporary)
        child = root / "child.py"
        pid_file = root / "child.pid"
        parallel_pid_file = root / "parallel.pid"
        child.write_text(
            "import os,sys,tempfile,time\n"
            "from pathlib import Path\n"
            "temporary = Path(tempfile.mkdtemp(prefix='dynlex-timeout-child-'))\n"
            "(temporary / 'artifact').write_bytes(b'child artifact')\n"
            "Path(sys.argv[1] + '.temporary').write_text(str(temporary))\n"
            "Path(sys.argv[1]).write_text(str(os.getpid()))\n"
            "time.sleep(60)\n", encoding="utf-8",
        )
        script = root / "nested.sh"
        script.write_text('PROJECT_DIR=$2\n' + wrapper + '''
if [[ "$1" == inner ]]; then
    run_with_timeout 30 "$3" "$4/child.py" "$4/child.pid" &
    run_with_timeout 30 "$3" "$4/child.py" "$4/parallel.pid" &
    wait
else
    run_with_timeout "$6" --isolate-temporaries true "$5" "$0" inner "$2" "$3" "$4" "$5"
fi
''', encoding="utf-8", newline="\n")
        pids = []
        try:
            started = time.monotonic()
            result = subprocess.run(
                [shell, script.as_posix(), "outer", project.as_posix(), sys.executable, root.as_posix(), shell,
                 "3" if os.name == "nt" else "1"],
                capture_output=True, text=True, timeout=10,
            )
            assert pid_file.exists() and parallel_pid_file.exists(), "parallel nested children never started"
            pids = [int(path.read_text()) for path in (pid_file, parallel_pid_file)]
            assert result.returncode == 124, (result.returncode, result.stdout, result.stderr)
            assert time.monotonic() - started < 10, "watchdog did not return promptly"
            assert all(not running(pid) for pid in pids), "outer timeout left a nested timed process running"
            assert all(not Path(path.read_text()).exists() for path in root.glob('*.temporary')), \
                "outer timeout left child temporary files behind"
        finally:
            for path in (pid_file, parallel_pid_file):
                if path.exists():
                    pid = int(path.read_text())
                    if running(pid):
                        if os.name == "nt":
                            subprocess.run(["taskkill", "/PID", str(pid), "/T", "/F"], check=True,
                                           stdout=subprocess.DEVNULL)
                        else:
                            os.kill(pid, signal.SIGKILL)
            for path in root.glob('*.temporary'):
                temporary_path = Path(path.read_text())
                if temporary_path.exists():
                    shutil.rmtree(temporary_path)


if __name__ == "__main__":
    main()
