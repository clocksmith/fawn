#!/usr/bin/env python3
"""Prove private owner header changes invalidate a real native object."""
from pathlib import Path
import subprocess
import tempfile


def main() -> None:
    owner = Path(__file__).resolve().parents[1]
    headers = (
        "../instance/src/internal.h",
        "../futures/src/internal.h",
        "../labels/src/internal.h",
        "../error-scopes/src/internal.h",
        "../../zig/vendor/webgpu-headers/webgpu.h",
    )
    with tempfile.TemporaryDirectory(prefix="doe-native-dependencies-") as directory:
        target = str(Path(directory) / "native/backend.o")
        command = ["make", "--no-print-directory", "-C", str(owner), f"BUILD={directory}"]
        subprocess.run(command + [target], check=True, stdout=subprocess.PIPE, text=True)
        subprocess.run(command + ["-q", target], check=True)
        for header in headers:
            # -W models a newer prerequisite without mutating a shared snapshot.
            result = subprocess.run(command + ["-q", "-W", header, target], check=False)
            if result.returncode != 1:
                raise RuntimeError(f"header change did not invalidate object: {header}")
            before = Path(target).stat().st_mtime_ns
            subprocess.run(command + ["-W", header, target], check=True,
                           stdout=subprocess.PIPE, text=True)
            if Path(target).stat().st_mtime_ns <= before:
                raise RuntimeError(f"header change did not rebuild object: {header}")
    print("PASS actual native object rebuild for four owner-private headers and pinned ABI header; no header bytes changed")


if __name__ == "__main__":
    main()
