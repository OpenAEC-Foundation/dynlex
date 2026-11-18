"""Run supervised tests in a Windows job whose lifetime covers their descendants."""

import ctypes
from ctypes import wintypes
from contextlib import contextmanager
import subprocess


_kernel = ctypes.WinDLL("kernel32", use_last_error=True)


def _function(name, result, *arguments):
    function = getattr(_kernel, name)
    function.restype = result
    function.argtypes = arguments
    return function


class _ThreadEntry(ctypes.Structure):
    _fields_ = [
        ("size", wintypes.DWORD),
        ("usage_count", wintypes.DWORD),
        ("thread_id", wintypes.DWORD),
        ("owner_process_id", wintypes.DWORD),
        ("base_priority", wintypes.LONG),
        ("delta_priority", wintypes.LONG),
        ("flags", wintypes.DWORD),
    ]


_create_job = _function("CreateJobObjectW", wintypes.HANDLE, ctypes.c_void_p, wintypes.LPCWSTR)
_open_process = _function("OpenProcess", wintypes.HANDLE, wintypes.DWORD, wintypes.BOOL, wintypes.DWORD)
_assign_process = _function("AssignProcessToJobObject", wintypes.BOOL, wintypes.HANDLE, wintypes.HANDLE)
_terminate_job = _function("TerminateJobObject", wintypes.BOOL, wintypes.HANDLE, wintypes.UINT)
_close_handle = _function("CloseHandle", wintypes.BOOL, wintypes.HANDLE)
_snapshot = _function("CreateToolhelp32Snapshot", wintypes.HANDLE, wintypes.DWORD, wintypes.DWORD)
_first_thread = _function("Thread32First", wintypes.BOOL, wintypes.HANDLE, ctypes.POINTER(_ThreadEntry))
_next_thread = _function("Thread32Next", wintypes.BOOL, wintypes.HANDLE, ctypes.POINTER(_ThreadEntry))
_open_thread = _function("OpenThread", wintypes.HANDLE, wintypes.DWORD, wintypes.BOOL, wintypes.DWORD)
_resume_thread = _function("ResumeThread", wintypes.DWORD, wintypes.HANDLE)

_CREATE_SUSPENDED = 0x00000004
_TH32CS_SNAPTHREAD = 0x00000004
_THREAD_SUSPEND_RESUME = 0x0002
_PROCESS_TERMINATE = 0x0001
_PROCESS_SET_QUOTA = 0x0100
_ERROR_NO_MORE_FILES = 18
_INVALID_HANDLE = ctypes.c_void_p(-1).value


@contextmanager
def _owned_handle(handle):
    if not handle or handle == _INVALID_HANDLE:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        yield handle
    finally:
        _close_handle(handle)


def _primary_thread(process_id):
    # CREATE_SUSPENDED keeps the primary thread from running any code that
    # could start other threads before the job owns the process.
    with _owned_handle(_snapshot(_TH32CS_SNAPTHREAD, 0)) as snapshot:
        entry = _ThreadEntry()
        entry.size = ctypes.sizeof(entry)
        more = _first_thread(snapshot, ctypes.byref(entry))
        while more:
            if entry.owner_process_id == process_id:
                return entry.thread_id
            entry.size = ctypes.sizeof(entry)
            more = _next_thread(snapshot, ctypes.byref(entry))
        error = ctypes.get_last_error()
        if error != _ERROR_NO_MORE_FILES:
            raise ctypes.WinError(error)
        raise RuntimeError(f"Suspended test process {process_id} has no primary thread")


class WindowsTestJob:
    def __init__(self):
        self.handle = _create_job(None, None)
        if not self.handle:
            raise ctypes.WinError(ctypes.get_last_error())

    def start(self, command, **options):
        process = subprocess.Popen(command, creationflags=_CREATE_SUSPENDED, **options)
        try:
            access = _PROCESS_SET_QUOTA | _PROCESS_TERMINATE
            with _owned_handle(_open_process(access, False, process.pid)) as child:
                if not _assign_process(self.handle, child):
                    raise ctypes.WinError(ctypes.get_last_error())
            thread_id = _primary_thread(process.pid)
            with _owned_handle(_open_thread(_THREAD_SUSPEND_RESUME, False, thread_id)) as thread:
                suspension_count = _resume_thread(thread)
                if suspension_count == 0xFFFFFFFF:
                    raise ctypes.WinError(ctypes.get_last_error())
                if suspension_count != 1:
                    raise RuntimeError(f"Unexpected suspension count for test thread {thread_id}: {suspension_count}")
        except BaseException:
            process.kill()
            process.wait()
            raise
        return process

    def terminate(self):
        if not _terminate_job(self.handle, 124):
            raise ctypes.WinError(ctypes.get_last_error())

    def close(self):
        _close_handle(self.handle)
