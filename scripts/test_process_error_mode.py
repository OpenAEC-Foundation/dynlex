"""Verify child crash reporting without displaying Windows error dialogs."""
import ctypes
import os
import subprocess
import sys
import unittest

from process_error_mode import unattended_child_processes


class ChildErrorModeTests(unittest.TestCase):
    def test_normal_child_exit_is_preserved(self):
        with unattended_child_processes():
            result = subprocess.run([sys.executable, "-c", "raise SystemExit(7)"], timeout=5)
        self.assertEqual(result.returncode, 7)

    @unittest.skipUnless(os.name == "nt", "Windows process error mode")
    def test_inheritance_restoration_and_failure_status(self):
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel.GetErrorMode.restype = ctypes.c_uint
        initial = kernel.GetErrorMode()
        with unattended_child_processes():
            child = subprocess.run(
                [sys.executable, "-c", "import ctypes; print(ctypes.windll.kernel32.GetErrorMode())"],
                text=True, capture_output=True, check=True, timeout=5,
            )
            self.assertEqual(int(child.stdout) & 3, 3)
            self.assertEqual(kernel.GetErrorMode() & initial, initial)
            with unattended_child_processes():
                pass
            self.assertEqual(kernel.GetErrorMode() & 3, 3)
            # ctypes translates RaiseException into a Python exception. Exit the
            # child with the native failure status to test transport separately.
            failure = subprocess.run(
                [sys.executable, "-c", "import ctypes; k=ctypes.WinDLL('kernel32'); "
                 "k.GetCurrentProcess.restype=ctypes.c_void_p; "
                 "k.TerminateProcess.argtypes=[ctypes.c_void_p, ctypes.c_uint]; "
                 "k.TerminateProcess(k.GetCurrentProcess(), 0xC0000094)"],
                capture_output=True, timeout=5,
            )
            self.assertEqual(failure.returncode & 0xFFFFFFFF, 0xC0000094)
        self.assertEqual(kernel.GetErrorMode(), initial)
        with self.assertRaisesRegex(RuntimeError, "test cleanup"):
            with unattended_child_processes():
                raise RuntimeError("test cleanup")
        self.assertEqual(kernel.GetErrorMode(), initial)


if __name__ == "__main__":
    unittest.main()
