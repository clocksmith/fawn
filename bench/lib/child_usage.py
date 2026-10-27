"""Read usage of an owned benchmark child without inventing unavailable counters."""

from __future__ import annotations

import ctypes
import os
import subprocess
import sys

if os.name != "nt":
    import resource


def child_cpu_snapshot() -> float | None:
    if os.name == "nt":
        return None
    usage = resource.getrusage(resource.RUSAGE_CHILDREN)
    return usage.ru_utime + usage.ru_stime


def mac_resident_kib(pid: int) -> int:
    """Sample resident bytes via the SDK's proc_taskinfo ABI, without ps children."""
    if sys.platform != "darwin":
        raise ValueError("macOS resident sampling requires Darwin")

    class TaskInfo(ctypes.Structure):
        _fields_ = [(name, ctypes.c_uint64) for name in (
            "virtual_size", "resident_size", "total_user", "total_system", "threads_user", "threads_system",
        )] + [(name, ctypes.c_int32) for name in (
            "policy", "faults", "pageins", "cow_faults", "messages_sent", "messages_received",
            "syscalls_mach", "syscalls_unix", "csw", "threadnum", "numrunning", "priority",
        )]

    library = ctypes.CDLL("/usr/lib/libproc.dylib", use_errno=True)
    info = library.proc_pidinfo
    info.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_uint64, ctypes.c_void_p, ctypes.c_int]
    info.restype = ctypes.c_int
    task = TaskInfo()
    received = info(pid, 4, 0, ctypes.byref(task), ctypes.sizeof(task))
    if received != ctypes.sizeof(task):
        # The child may exit between poll() and sampling. No resident observation
        # is claimed for that interval; an earlier sample remains its lower bound.
        if received == 0 and ctypes.get_errno() in (0, 3):
            return 0
        raise OSError(ctypes.get_errno(), "proc_pidinfo task sample failed")
    return task.resident_size // 1024


def windows_child_usage(process: subprocess.Popen) -> tuple[float, int]:
    """Return CPU milliseconds and peak working-set KiB from the owned OS handle.

    CPython's Windows Popen retains its CreateProcess handle through wait(); using
    that handle avoids reopening a PID which could already belong to another child.
    These counters cover this process, not an aggregate descendant tree or VRAM.
    """
    from ctypes import wintypes

    class MemoryCounters(ctypes.Structure):
        _fields_ = [("cb", wintypes.DWORD), ("PageFaultCount", wintypes.DWORD)] + [
            (name, ctypes.c_size_t) for name in (
                "PeakWorkingSetSize", "WorkingSetSize", "QuotaPeakPagedPoolUsage",
                "QuotaPagedPoolUsage", "QuotaPeakNonPagedPoolUsage", "QuotaNonPagedPoolUsage",
                "PagefileUsage", "PeakPagefileUsage",
            )
        ]

    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    times = kernel.GetProcessTimes
    times.argtypes = [wintypes.HANDLE] + [ctypes.POINTER(wintypes.FILETIME)] * 4
    times.restype = wintypes.BOOL
    creation, exit_time, kernel_time, user_time = (wintypes.FILETIME() for _ in range(4))
    handle = wintypes.HANDLE(int(process._handle))
    if not times(handle, ctypes.byref(creation), ctypes.byref(exit_time), ctypes.byref(kernel_time), ctypes.byref(user_time)):
        raise ctypes.WinError(ctypes.get_last_error())
    memory = MemoryCounters()
    memory.cb = ctypes.sizeof(memory)
    read_memory = ctypes.WinDLL("psapi", use_last_error=True).GetProcessMemoryInfo
    read_memory.argtypes = [wintypes.HANDLE, ctypes.POINTER(MemoryCounters), wintypes.DWORD]
    read_memory.restype = wintypes.BOOL
    if not read_memory(handle, ctypes.byref(memory), memory.cb):
        raise ctypes.WinError(ctypes.get_last_error())
    ticks = sum((value.dwHighDateTime << 32) | value.dwLowDateTime for value in (kernel_time, user_time))
    return ticks / 10_000.0, memory.PeakWorkingSetSize // 1024


def completed_child_cpu_ms(process: subprocess.Popen, before: float | None) -> float:
    if process.poll() is None:
        raise ValueError("child CPU completion requires a terminated child")
    if os.name == "nt":
        return windows_child_usage(process)[0]
    if before is None:
        raise ValueError("missing POSIX child CPU snapshot")
    after = child_cpu_snapshot()
    return max(0.0, (after - before) * 1000.0)
