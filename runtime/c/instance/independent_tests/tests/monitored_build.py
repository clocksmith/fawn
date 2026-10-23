#!/usr/bin/env python3
"""Bound one source-build process group and preserve observable lifecycle."""
from __future__ import annotations

import json
import os
from pathlib import Path
import signal
import subprocess
import time
from typing import Any


def processes(group: int) -> list[dict[str, Any]]:
    rows = []
    for path in Path('/proc').glob('[0-9]*'):
        try:
            fields = (path / 'stat').read_text().split(') ', 1)[1].split()
            if int(fields[2]) != group:
                continue
            rows.append({'pid': int(path.name), 'ppid': int(fields[1]),
                         'pgrp': int(fields[2]), 'state': fields[0],
                         'cpu_user_ticks': int(fields[11]),
                         'cpu_system_ticks': int(fields[12]),
                         'resident_pages': int(fields[21]),
                         'wait_channel': (path / 'wchan').read_text().strip(),
                         'argv': (path / 'cmdline').read_bytes().decode(
                             errors='replace').split('\0')[:-1]})
        except (FileNotFoundError, ProcessLookupError):
            continue
    return sorted(rows, key=lambda row: row['pid'])


def run_build(label: str, argv: list[str], cwd: Path,
              environment: dict[str, str], receipts: Path,
              timeout: int = 900) -> dict[str, Any]:
    """Capture build state without changing compiler arguments or inputs."""
    stdout_path = receipts / (label + '.stdout')
    stderr_path = receipts / (label + '.stderr')
    samples_path = receipts / (label + '.samples.jsonl')
    start = time.monotonic()
    with stdout_path.open('x') as output, stderr_path.open('x') as error:
        process = subprocess.Popen(argv, cwd=cwd, env=environment,
                                   stdout=output, stderr=error,
                                   start_new_session=True)
        next_sample = start
        is_timeout = False
        with samples_path.open('x') as samples:
            while process.poll() is None:
                now = time.monotonic()
                if now >= next_sample:
                    memory = Path('/proc/meminfo').read_text().splitlines()
                    sample = {'elapsed_seconds': now - start,
                              'processes': processes(process.pid),
                              'memory': [line for line in memory if
                                         line.split(':')[0] in
                                         ['MemAvailable', 'MemFree',
                                          'SwapFree', 'SwapTotal']],
                              'tmp_available_bytes': os.statvfs('/tmp').f_bavail
                                  * os.statvfs('/tmp').f_frsize}
                    samples.write(json.dumps(sample) + '\n')
                    samples.flush()
                    print(label, round(now-start, 2),
                          [(p['pid'], p['state'], p['wait_channel'])
                           for p in sample['processes']], flush=True)
                    next_sample = now + 30
                if now - start >= timeout:
                    is_timeout = True
                    break
                time.sleep(0.25)
        before_cleanup = processes(process.pid)
        signals = []
        # Only this wrapper's new session/process group is signalled.
        if process.poll() is None or any(p['state'] != 'Z'
                                         for p in before_cleanup):
            for sig, limit in [(signal.SIGTERM, 5), (signal.SIGKILL, 5)]:
                if not any(p['state'] != 'Z' for p in processes(process.pid)):
                    break
                os.killpg(process.pid, sig)
                signals.append(sig.name)
                deadline = time.monotonic() + limit
                while time.monotonic() < deadline:
                    process.poll()
                    if not any(p['state'] != 'Z'
                               for p in processes(process.pid)):
                        break
                    time.sleep(0.1)
        returncode = process.wait(timeout=10)
        after_cleanup = processes(process.pid)
    row = {'argv': argv, 'cwd': str(cwd), 'returncode': returncode,
           'timed_out': is_timeout, 'timeout_seconds': timeout,
           'elapsed_seconds': time.monotonic() - start,
           'stdout': stdout_path.read_text(errors='replace'),
           'stderr': stderr_path.read_text(errors='replace'),
           'environment': {k: environment[k] for k in
                           ['DOE_BACKEND', 'LD_LIBRARY_PATH']
                           if k in environment},
           'process_group': process.pid,
           'pid_namespace': os.readlink('/proc/self/ns/pid'),
           'before_cleanup': before_cleanup,
           'cleanup_signals': signals, 'after_cleanup': after_cleanup,
           'no_live_process_group_members': all(p['state'] == 'Z'
                                               for p in after_cleanup)}
    (receipts / (label + '.json')).write_text(
        json.dumps(row, indent=2, sort_keys=True) + '\n', encoding='utf-8')
    print(label, 'TERMINAL', returncode, 'TIMEOUT', is_timeout,
          'QUIESCENT', row['no_live_process_group_members'], flush=True)
    assert row['no_live_process_group_members'], row
    return row
