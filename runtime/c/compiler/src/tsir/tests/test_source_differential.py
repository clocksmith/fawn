#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compare canonical semantic SHA-256, including typed rejections, with real Zig."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[6]
COMPILER = ROOT / "runtime/zig/src/compiler"
PRELUDE = "@compute @workgroup_size(1) fn main() {"
STORAGE = "@group(0) @binding(0) var<storage,read_write> output: array<u32>;"
CASES = [
    ("empty", PRELUDE + "}", 0),
    ("while", PRELUDE + "var i = 0u; while i < 3u { i += 1u; } }", 1),
    ("loop-break", PRELUDE + "loop { break; } }", 1),
    ("continuing-local", STORAGE + PRELUDE + "var i = 0u; loop { let limit = 3u; i += 1u; continuing { output[0u] = i; break if i >= limit; } } }", 1),
    ("continuing-for", STORAGE + PRELUDE + "var i = 0u; loop { i += 1u; continuing { for (var j = 0u; j < 2u; j += 1u) { output[j] = i; } break if i >= 3u; } } }", 1),
    ("switch-nested", STORAGE + PRELUDE + "let choice = 1u; switch choice { case 0u, 1u: { let value = 2u; output[0u] = value; } default: { let value = 3u; switch value { case 3u: { output[0u] = value; } default: {} } } } }", 0),
    ("switch-loop-projection", STORAGE + PRELUDE + "switch 1u { case 1u: { var i = 0u; while i < 2u { output[i] = i; i += 1u; } } default: {} } }", 0),
    ("mixed-nesting", STORAGE + PRELUDE + "var count = 0u; if count == 0u { loop { count += 1u; if count > 2u { break; } continuing { output[0u] = count; } } } }", 1),
    ("decreasing-for", PRELUDE + "for (var i = 4u; i > 0u; i -= 1u) {} }", 1),
    ("local-const-bound", PRELUDE + "const limit = 4u; for (var i = 0u; i < limit; i += 1u) {} }", 0),
    ("local-let-bound", PRELUDE + "let limit = 4u; for (var i = 0u; i < limit; i += 1u) {} }", 0),
    ("phony-assignment", STORAGE + PRELUDE + "_ = output[0u]; }", 0),
    ("opaque-sampler", "@group(0) @binding(0) var s: sampler;" + PRELUDE + "_ = s; }", 1),
    ("opaque-comparison-sampler", "@group(0) @binding(0) var s: sampler_comparison;" + PRELUDE + "_ = s; }", 1),
    ("opaque-texture-2d", "@group(0) @binding(0) var t: texture_2d<f32>;" + PRELUDE + "_ = textureDimensions(t); }", 1),
    ("opaque-depth-2d", "@group(0) @binding(0) var t: texture_depth_2d;" + PRELUDE + "_ = textureDimensions(t); }", 1),
    ("opaque-storage-2d", "@group(0) @binding(0) var t: texture_storage_2d<rgba8unorm, write>;" + PRELUDE + "_ = textureDimensions(t); }", 1),
    ("packed-indirect-unpack", "requires packed_4x8_integer_dot_product;" + STORAGE +
     "@group(0) @binding(1) var<storage,read> indices: array<u32>;"
     "@group(0) @binding(2) var<storage,read> table: array<u32>;" + PRELUDE +
     "for (var i = 0u; i < 4u; i += 1u) { output[i] = table[unpack4xU8(indices[i]).x]; } }", 0),
    ("packed-all-runtime", "requires packed_4x8_integer_dot_product;" + STORAGE + PRELUDE +
     "var u = vec4u(1u,2u,3u,4u); var v = vec4i(-1,2,-3,4); var p = output[0u];"
     "output[0u] = pack4xU8(u); output[1u] = pack4xI8(v);"
     "output[2u] = pack4xU8Clamp(u); output[3u] = pack4xI8Clamp(v);"
     "output[4u] = unpack4xU8(p).x; output[5u] = bitcast<u32>(unpack4xI8(p).y);"
     "output[6u] = dot4U8Packed(p,p); output[7u] = bitcast<u32>(dot4I8Packed(p,p)); }", 0),
    ("packed-abstract-arguments", "requires packed_4x8_integer_dot_product;" + STORAGE + PRELUDE +
     "output[0u] = unpack4xU8(7 + 1).x;"
     "output[1u] = dot4U8Packed(1 + 2, 4); output[2u] = pack4xU8(vec4(1,2,3,4)); }", 0),
    ("ldexp-abstract", STORAGE + PRELUDE +
     "output[0u] = u32(ldexp(1 + 1, 3)); output[1u] = u32(ldexp(vec2(1.0,2.0),vec2(3,4)).y); }", 0),


]


# Known frontend admission divergence, not an equal-success semantic case.
# Zig sema.analyze_local_decl materializes inferred const to i32, which its
# packed integer inferType rejects. C currently keeps the abstract constant.
ADMISSION_DIVERGENCES = [
    ("ldexp-inferred-integer-vector-admission", STORAGE + PRELUDE +
     "output[0u] = u32(ldexp(vec2(1,2),vec2(3,4)).y); }", b"error: TypeMismatch"),
    ("packed-local-const-admission", "requires packed_4x8_integer_dot_product;" + STORAGE + PRELUDE +
     "const word = 7; output[0u] = unpack4xU8(word + 1).x; }", b"error: TypeMismatch"),
]


def run(args: list[str], timeout: int = 30) -> subprocess.CompletedProcess[bytes]:
    child = subprocess.Popen(args, cwd=ROOT, stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE, start_new_session=True)
    try:
        stdout, stderr = child.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        os.killpg(child.pid, signal.SIGKILL)
        child.communicate()
        raise
    return subprocess.CompletedProcess(args, child.returncode, stdout, stderr)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--c-driver", type=Path, default=ROOT / "runtime/c/compiler/build/test_source_differential")
    parser.add_argument("--zig", default=os.environ.get("ZIG", "zig"))
    args = parser.parse_args()
    zig = shutil.which(args.zig)
    if not zig or run([zig, "version"]).stdout.strip() != b"0.15.2":
        raise SystemExit("pinned Zig 0.15.2 is required")
    with tempfile.TemporaryDirectory(prefix="tsir-source-differential-") as directory:
        temp = Path(directory)
        # Use the unmodified public reference digest API; neither serializer
        # nor frontend implementation is rewritten for this comparison.
        with tempfile.NamedTemporaryFile(mode="w", suffix=".zig", prefix="source_diff_", dir=COMPILER.parent) as helper:
            helper.write('''const std = @import("std");
const parser = @import("compiler/wgsl/frontend/parser.zig");
const sema = @import("compiler/wgsl/frontend/sema.zig");
const builder = @import("compiler/wgsl/ir/ir_builder.zig");
const frontend = @import("compiler/tsir/frontend.zig");
const digest = @import("compiler/tsir/digest.zig");
const schema = @import("compiler/tsir/schema.zig");
pub fn main() !void {
    var arena = std.heap.ArenaAllocator.init(std.heap.page_allocator);
    defer arena.deinit();
    const allocator = arena.allocator();
    const args = try std.process.argsAlloc(allocator);
    if (args.len != 2) return error.InvalidArguments;
    const source = try std.fs.cwd().readFileAlloc(allocator, args[1], 1 << 20);
    var tree = try parser.parseSource(allocator, source);
    var analyzed = try sema.analyze(allocator, &tree);
    var ir = try builder.build(allocator, &tree, &analyzed);
    var source_digest: [32]u8 = undefined;
    std.crypto.hash.sha2.Sha256.hash(source, &source_digest, .{});
    const semantic = try frontend.lowerIrToTsir(allocator, &ir, source_digest, "source-differential-v1");
    const realization = schema.Realization{.functions = &.{}, .emitter_digest = [_]u8{0} ** 32, .rejections = &.{}};
    const hashes = try digest.compute(allocator, semantic, realization, "");
    const writer = std.fs.File.stdout().deprecatedWriter();
    for (hashes.semantic) |byte| try writer.print("{x:0>2}", .{byte});
    try writer.print(" {d}\\n", .{semantic.rejections.len});
}
''')
            helper.flush()
            binary = temp / "zig-source"
            build = run([zig, "build-exe", helper.name, "-lc", "-O", "Debug",
                         "--cache-dir", str(temp / "cache"), "--global-cache-dir", str(temp / "global"),
                         "-femit-bin=" + str(binary)], timeout=120)
            assert build.returncode == 0, build.stderr.decode(errors="replace")
            for name, source, rejection_count in CASES:
                path = temp / (name + ".wgsl")
                path.write_text(source)
                candidate = run([str(args.c_driver.resolve()), str(path)])
                reference = run([str(binary), str(path)])
                assert candidate.returncode == reference.returncode == 0, (name, candidate, reference)
                reference_digest, reference_rejections = reference.stdout.decode().split()
                candidate_digest = hashlib.sha256(candidate.stdout).hexdigest()
                if candidate_digest != reference_digest:
                    raise AssertionError(f"{name}: semantic SHA-256 mismatch\nC: {candidate_digest} {candidate.stdout.decode()}\nZig: {reference_digest}")
                assert int(reference_rejections) == rejection_count, (name, reference_rejections)
                semantic = json.loads(candidate.stdout)
                assert len(semantic["rejections"]) == rejection_count, (name, semantic["rejections"])
                print(f"source semantic differential: {name} canonical SHA-256 matches, {rejection_count} retained rejections")
            for name, source, diagnostic in ADMISSION_DIVERGENCES:
                path = temp / (name + ".wgsl")
                path.write_text(source)
                candidate = run([str(args.c_driver.resolve()), str(path)])
                reference = run([str(binary), str(path)])
                assert candidate.returncode == 0, (name, candidate)
                assert reference.returncode != 0 and diagnostic in reference.stderr, (name, reference)
                print(f"KNOWN ADMISSION GAP: {name}: C accepts; Zig rejects TypeMismatch (not parity)")
    print(f"TSIR source semantic differential: {len(CASES)} canonical semantic SHA-256 cases passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
