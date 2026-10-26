#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Exercise public source-to-DXC publication using a controlled child, not a GPU."""
from __future__ import annotations

import argparse
import ctypes
from pathlib import Path
import struct
import tempfile


class Buffer(ctypes.Structure):
    _fields_ = [("data", ctypes.c_void_p), ("size", ctypes.c_size_t)]


class Diagnostic(ctypes.Structure):
    _fields_ = [("domain", ctypes.c_uint32), ("code", ctypes.c_uint32),
                ("line", ctypes.c_uint32), ("column", ctypes.c_uint32),
                ("system_error", ctypes.c_int32), ("stage", ctypes.c_char * 48),
                ("message", ctypes.c_char * 256)]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--library", required=True, type=Path,
                        help="Built public host companion")
    parser.add_argument("--child", required=True, type=Path,
                        help="Built test_dxil_io controlled child executable")
    parser.add_argument("--real-dxc", type=Path,
                        help="Also exercise an explicitly selected actual DXC")
    args = parser.parse_args()
    library = ctypes.CDLL(str(args.library.resolve()))
    translate = library.doe_compiler_host_dxil
    translate.argtypes = [ctypes.c_uint32, ctypes.c_char_p, ctypes.c_size_t,
                          ctypes.c_char_p, ctypes.c_char_p,
                          ctypes.POINTER(Buffer), ctypes.c_size_t,
                          ctypes.POINTER(Diagnostic), ctypes.c_size_t]
    translate.restype = ctypes.c_int
    release = library.doe_compiler_host_release
    release.argtypes = [ctypes.POINTER(Buffer)]
    release.restype = None
    child = str(args.child.resolve()).encode()
    with tempfile.TemporaryDirectory(prefix="doe public host DXIL ") as name:
        scratch = Path(name)

        def invoke(source: bytes, executable: bytes = child) -> tuple[int, bytes, Diagnostic]:
            output = Buffer()
            diagnostic = Diagnostic()
            status = translate(1, source, len(source), executable,
                               str(scratch).encode(), ctypes.byref(output),
                               ctypes.sizeof(output), ctypes.byref(diagnostic),
                               ctypes.sizeof(diagnostic))
            try:
                if status != 0:
                    assert not output.data and output.size == 0, "failed publication"
                    return status, b"", diagnostic
                assert output.data and output.size > 0
                return status, ctypes.string_at(output.data, output.size), diagnostic
            finally:
                release(ctypes.byref(output))
                assert not output.data and output.size == 0
                release(ctypes.byref(output))

        def shader(entry: str) -> bytes:
            return ("@compute @workgroup_size(1) fn " + entry + "() {}").encode()

        status, binary, _ = invoke(shader("valid"))
        assert status == 0 and binary[:4] == b"DXBC"
        assert struct.unpack_from("<I", binary, 24)[0] == len(binary)
        assert len(binary) < 256
        assert list(scratch.iterdir()) == [], "successful call leaked temporary files"
        for entry, expected, detail in (
            ("missing", 7, b"DXC reported success"),
            ("failure", 7, b"DXC failed"),
            ("malformed", 7, b""),
            ("oversized", 5, b""),
        ):
            status, _, diagnostic = invoke(shader(entry))
            assert status == expected, (entry, status, bytes(diagnostic.message))
            assert diagnostic.domain == 2
            assert 0 < len(diagnostic.message) <= 255
            assert detail in diagnostic.message, (entry, diagnostic.message)
            assert list(scratch.iterdir()) == [], entry + " leaked temporary files"
            # A failed request must not poison the next ordinary successful one.
            assert invoke(shader("valid"))[0] == 0
        status, _, diagnostic = invoke(shader("valid"), b"/absent-doe-dxc")
        assert status == 8 and diagnostic.domain == 2
        status, _, diagnostic = invoke(b"@compute fn broken( {}")
        assert status == 7 and diagnostic.domain == 2 and diagnostic.message
        assert list(scratch.iterdir()) == []
        if args.real_dxc is not None:
            status, binary, diagnostic = invoke(shader("valid"),
                str(args.real_dxc.resolve()).encode())
            assert status == 0, (status, diagnostic.message)
            assert binary[:4] == b"DXBC"
            assert struct.unpack_from("<I", binary, 24)[0] == len(binary)
            assert list(scratch.iterdir()) == []
            print("PASS: public source-to-DXIL ABI with explicitly selected actual DXC")
    print("PASS: public DXIL ABI source/child/validation, failure atomicity, cleanup and retry")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
