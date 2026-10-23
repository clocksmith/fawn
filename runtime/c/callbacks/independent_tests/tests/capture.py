#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Capture one independent evaluation command and its first outcome."""
from __future__ import annotations

import argparse
import datetime
import json
from pathlib import Path
import resource
import shutil
import subprocess
import time


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--receipt", type=Path, required=True)
    parser.add_argument("--cwd", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=180)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    if args.receipt.exists():
        raise SystemExit(f"Refusing to replace preserved receipt: {args.receipt}")
    started = time.monotonic()
    free_before = shutil.disk_usage(args.cwd).free
    timed_out = False
    try:
        result = subprocess.run(command, cwd=args.cwd, capture_output=True,
                                text=True, timeout=args.timeout, check=False)
        code = result.returncode
        stdout = result.stdout
        stderr = result.stderr
    except subprocess.TimeoutExpired as error:
        timed_out = True
        code = None
        stdout = error.stdout or b""
        stderr = error.stderr or b""
        if isinstance(stdout, bytes):
            stdout = stdout.decode(errors="replace")
        if isinstance(stderr, bytes):
            stderr = stderr.decode(errors="replace")
    receipt = {
        "command": command, "cwd": str(args.cwd), "returncode": code,
        "timed_out": timed_out, "elapsed_seconds": time.monotonic() - started,
        "finished_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "max_rss_kib": resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss,
        "free_bytes_before": free_before, "free_bytes_after": shutil.disk_usage(args.cwd).free,
        "stdout": stdout, "stderr": stderr,
    }
    args.receipt.write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps(receipt, indent=2))
    return 124 if timed_out else int(code)


if __name__ == "__main__":
    raise SystemExit(main())
