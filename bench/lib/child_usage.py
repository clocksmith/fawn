"""Read usage of an owned benchmark child without inventing unavailable counters."""

from __future__ import annotations

import ctypes
import os
import subprocess

if os.name != "nt":
    import resource


def child_cpu_snapshot() -> float | None:
    if os.name == "nt":
        return None
    usage = resource.getrusage(resource.RUSAGE_CHILDREN)
    return usage.ru_utime + usage.ru_stime


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
