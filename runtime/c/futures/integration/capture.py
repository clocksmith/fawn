#!/usr/bin/env python3
"""Capture one source-bound command, output and observed process cost."""
from __future__ import annotations

import json
import pathlib
import resource
import subprocess
import sys
import time

output = pathlib.Path(sys.argv[1])
command = sys.argv[2:]
started = time.monotonic_ns()
result = subprocess.run(command, capture_output=True, text=True, check=False)
receipt = {
    "command": command,
    "cwd": str(pathlib.Path.cwd()),
    "returncode": result.returncode,
    "elapsed_ms": (time.monotonic_ns() - started) / 1_000_000,
    "child_peak_rss_kib": resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss,
    "stdout": result.stdout,
    "stderr": result.stderr,
}
output.write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
print(result.stdout, end="")
print(result.stderr, end="", file=sys.stderr)
print(f"receipt: {output}; exit {result.returncode}")
sys.exit(result.returncode)
