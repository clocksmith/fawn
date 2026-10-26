#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Differential retained-Zig target/emitter identities and bounded I/O failures."""
import hashlib
import os
from pathlib import Path
import shutil
import signal
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[6]
TSIR = ROOT / "runtime/c/compiler/src/tsir"
ZIG_TSIR = ROOT / "runtime/zig/src/compiler/tsir"


def run(args, cwd=ROOT):
    proc = subprocess.Popen(args, cwd=cwd, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, text=True, start_new_session=True)
    try:
        stdout, stderr = proc.communicate(timeout=60)
    except subprocess.TimeoutExpired:
        os.killpg(proc.pid, signal.SIGKILL)
        stdout, stderr = proc.communicate()
        raise AssertionError(f"timed out: {args!r}\n{stderr}")
    if proc.returncode:
        raise AssertionError(f"exit {proc.returncode}: {args!r}\n{stderr}\n{stdout}")
    return stdout


def main():
    zig = shutil.which("zig")
    if not zig:
        raise SystemExit("Zig 0.15.2 is required for this differential check")
    if run([zig, "version"]).strip() != "0.15.2":
        raise SystemExit("differential check requires pinned Zig 0.15.2")
    with tempfile.TemporaryDirectory(prefix="tsir-targets-") as directory:
        temp = Path(directory)
        binary = temp / "targets"
        run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra",
             "-Wpedantic", "-Werror", "-Wconversion", "-Wswitch-enum",
             "-I" + str(TSIR), "-I" + str(TSIR / "tests"),
             str(TSIR / "tests/test_targets.c"), str(TSIR / "tsir_targets.c"),
             str(TSIR / "tsir_sha256.c"), str(TSIR / "tsir_text.c"), str(TSIR / "tsir_file.c"), "-o", str(binary)])
        print(run([str(binary)]), end="")
        c_digests = run([str(binary), "--digests", str(ROOT)]).splitlines()
        # The reference imports sibling compiler modules. Keep this temporary
        # root at their common module boundary; it is always removed afterward.
        with tempfile.NamedTemporaryFile(mode="w", suffix=".zig", prefix="identity_oracle_",
                                         dir=ROOT / "runtime/zig/src/compiler") as source:
            source.write('''const std = @import("std");
const targets = @import("targets/mod.zig");
const webgpu = @import("tsir/emit_webgpu.zig");
const msl = @import("tsir/emit_msl.zig");
const spirv = @import("tsir/emit_spir_v.zig");
const dxil = @import("tsir/emit_dxil.zig");
pub fn main() !void {
    const stdout = std.fs.File.stdout().deprecatedWriter();
    for ([_][32]u8{
        targets.descriptorHash(targets.webgpu_generic.descriptor),
        targets.descriptorHash(targets.msl.descriptor),
        targets.descriptorHash(targets.spir_v.descriptor),
        webgpu.emitterCodeDigest(), msl.emitterCodeDigest(), spirv.emitterCodeDigest(),
        dxil.emitterCodeDigest(),
    }) |digest| {
        for (digest) |byte| try stdout.print("{x:0>2}", .{byte});
        try stdout.writeAll("\\n");
    }
}
''')
            source.flush()
            zig_digests = run([zig, "run", "--cache-dir", str(temp / "cache"),
                               "--global-cache-dir", str(temp / "global"),
                               source.name]).splitlines()
        assert c_digests == zig_digests, (c_digests, zig_digests)
        assert len(c_digests) == 7
        missing = temp / "missing"
        untouched = "a5" * 32
        assert run([str(binary), "--emitter", "0", str(missing)]).strip() == "2 " + untouched
        assert run([str(binary), "--emitter", "99", str(ROOT)]).strip() == "2 " + untouched
        assert run([str(binary), "--backend", "1", str(ROOT)]).strip() == "3 " + untouched
        assert run([str(binary), "--backend", "99", str(ROOT)]).strip() == "2 " + untouched
        assert run([str(binary), "--invalid-utf8"]).strip() == "2 " + untouched
        source_root = temp / "sources-λ"
        source_dir = source_root / "runtime/zig/src/compiler/tsir"
        source_dir.mkdir(parents=True)
        content = {"emit_webgpu.zig": b"emitter", "emit_kernel_body.zig": b"body",
                   "emit_semantic_metadata.zig": b"metadata"}
        for name, data in content.items():
            (source_dir / name).write_bytes(data)
        expected = hashlib.sha256(b"emitterbodymetadata").hexdigest()
        assert run([str(binary), "--emitter", "0", str(source_root)]).strip() == "0 " + expected
        (source_dir / "emit_dxil.zig").write_bytes(b"dxil")
        (source_dir / "emit_text_skeleton.zig").write_bytes(b"common")
        dxil_expected = hashlib.sha256(b"dxilcommonbodymetadata").hexdigest()
        assert run([str(binary), "--backend", "3", str(source_root)]).strip() == "0 " + dxil_expected
        (source_dir / "emit_text_skeleton.zig").unlink()
        assert run([str(binary), "--backend", "3", str(source_root)]).strip() == "2 " + untouched
        first = source_dir / "emit_webgpu.zig"
        first.write_bytes(b"x" * (1024 * 1024 + 1))
        assert run([str(binary), "--emitter", "0", str(source_root)]).strip() == "4 " + untouched
        first.unlink()
        first.mkdir()
        assert run([str(binary), "--emitter", "0", str(source_root)]).strip() == "2 " + untouched
        first.rmdir()
        os.mkfifo(first)
        assert run([str(binary), "--emitter", "0", str(source_root)]).strip() == "2 " + untouched
        print("targets differential: 7 Zig identities, 11 source/error checks (including non-ASCII path and invalid UTF-8) passed")


if __name__ == "__main__":
    main()
