"""Keep unattended child failures observable without Windows error dialogs."""
from contextlib import contextmanager
import os


@contextmanager
def unattended_child_processes():
    """Children inherit this process's error mode; restore it on context exit.

    These sequential test launchers change only their own process, never system
    settings. Exit codes and exceptions remain unchanged. CREATE_NO_WINDOW alone
    does not suppress Windows fault-reporting dialogs.
    """
    if os.name != "nt":
        yield
        return

    import ctypes

    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.GetErrorMode.argtypes = []
    kernel.GetErrorMode.restype = ctypes.c_uint
    kernel.SetErrorMode.argtypes = [ctypes.c_uint]
    kernel.SetErrorMode.restype = ctypes.c_uint
    previous = kernel.GetErrorMode()
    # SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX; inherited by child processes.
    kernel.SetErrorMode(previous | 0x0003)
    try:
        yield
    finally:
        kernel.SetErrorMode(previous)
