"""Exercise real mapped Mach-O cache identity without opening a GPU device."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile


def uuid_offset(data: bytes | bytearray) -> int:
    if len(data) < 32 or struct.unpack_from("<I", data)[0] != 0xFEEDFACF:
        raise ValueError("expected a thin 64-bit native Mach-O executable")
    count, command_bytes = struct.unpack_from("<II", data, 16)
    end = 32 + command_bytes
    if end > len(data):
        raise ValueError("truncated Mach-O load commands")
    offset = 32
    found: int | None = None
    for _ in range(count):
        if offset + 8 > end:
            raise ValueError("truncated Mach-O load command")
        kind, size = struct.unpack_from("<II", data, offset)
        if size < 8 or size > end - offset:
            raise ValueError("invalid Mach-O load command length")
        if kind == 0x1B:  # LC_UUID
            if size != 24 or found is not None:
                raise ValueError("expected one complete LC_UUID command")
            found = offset + 8
        offset += size
    if found is None:
        raise ValueError("native executable has no LC_UUID command")
    return found


def run(command: list[str], environment: dict[str, str]) -> None:
    result = subprocess.run(
        command, env=environment, capture_output=True, text=True, timeout=20,
    )
    if result.returncode:
        raise RuntimeError(
            f"{command!r} returned {result.returncode}\n"
            f"{result.stdout}{result.stderr}"
        )
    print(result.stdout, end="")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path,
                        help="Existing native shader-cache control executable.")
    parser.add_argument("--changed-binary", required=True, type=Path,
                        help="Control built with DOE_CACHE_IDENTITY_VARIANT=1.")
    args = parser.parse_args()
    binary = args.binary.resolve(strict=True)
    changed = args.changed_binary.resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix="doe-macho-cache-") as temporary:
        root = Path(temporary)
        cache = root / "cache"
        cache.mkdir()
        environment = {**os.environ, "DOE_SHADER_CACHE_DIR": str(cache)}
        run([str(binary), "warm"], environment)
        run([str(binary), "reload"], environment)
        run([str(changed), "reload-miss"], environment)

        # New macOS dyld rejects an absent LC_UUID before main. A zero UUID
        # remains loadable and exercises the runtime's own fail-closed guard.
        zero_uuid = root / "shader-cache-zero-uuid"
        shutil.copy2(binary, zero_uuid)
        data = bytearray(zero_uuid.read_bytes())
        offset = uuid_offset(data)
        data[offset:offset + 16] = bytes(16)
        zero_uuid.write_bytes(data)
        run(["codesign", "--force", "--sign", "-", "--timestamp=none",
             str(zero_uuid)], environment)
        signed = zero_uuid.read_bytes()
        offset = uuid_offset(signed)
        if signed[offset:offset + 16] != bytes(16):
            raise RuntimeError("codesign changed the zero-UUID control")
        run([str(zero_uuid), "identity-absent"], environment)
    print("Mach-O cache identity: fresh-process hit, changed-image miss and "
          "zero-UUID rejection passed (host loaded-image control; no GPU)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
