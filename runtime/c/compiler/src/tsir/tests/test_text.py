#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Length-bearing TSIR text controls and actual retained Zig slice identities."""
from pathlib import Path
import os
import shutil
import signal
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[6]
OWNER = ROOT / "runtime/c/compiler/src/tsir"


def run(args):
    child = subprocess.Popen(args, cwd=ROOT, stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE, start_new_session=True)
    try:
        stdout, stderr = child.communicate(timeout=120)
    except subprocess.TimeoutExpired:
        os.killpg(child.pid, signal.SIGKILL)
        child.communicate()
        raise
    assert child.returncode == 0, (args, stderr.decode(errors="replace"), stdout)
    return stdout


def main():
    zig = shutil.which(os.environ.get("ZIG", "zig"))
    assert zig and run([zig, "version"]).strip() == b"0.15.2", "pinned Zig required"
    with tempfile.TemporaryDirectory(prefix="tsir-text-") as directory:
        temp = Path(directory)
        binary = temp / "text"
        sources = [str(path) for path in sorted(OWNER.glob("*.c"))
                   if not path.name.startswith("tsir_source") and path.name != "tsir_bootstrap_manifest.c"]
        run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Wpedantic",
             "-Werror", "-Wconversion", "-Wswitch-enum", "-Wshadow",
             "-Wstrict-prototypes", "-Wmissing-prototypes", "-fno-fast-math", "-ffp-contract=off",
             "-I" + str(OWNER), str(OWNER / "tests/test_text.c"), *sources,
             "-lm", "-o", str(binary)])
        print(run([str(binary)]).decode(), end="")
        actual = run([str(binary), "--identities"]).splitlines()
        with tempfile.NamedTemporaryFile(mode="w", suffix=".zig", prefix="text_oracle_",
                                         dir=ROOT / "runtime/zig/src/compiler") as source:
            source.write(r'''const std = @import("std");
const s = @import("tsir/schema.zig");
const d = @import("tsir/digest.zig");
const t = @import("targets/mod.zig");
const skeleton = @import("tsir/emit_text_skeleton.zig");
pub fn main() !void {
    const text = "a\x00b";
    const zero = [_]u8{0} ** 32;
    const exactness = s.Exactness{.class = .tolerance_bounded,
        .tolerance_metric = text, .tolerance_epsilon = 0.5};
    const rejection = [_]s.RejectionEntry{.{.reason = .tsir_target_unfit,
        .node_path = text, .detail = text}};
    const semantic = s.Semantic{.frontend_version = text, .rejections = &rejection,
        .functions = &.{.{.name = text, .family_hint = .unknown,
            .axes = &.{.{.name = text, .lower_bound = text, .upper_bound = text, .step = text}},
            .bindings = &.{.{.name = text, .group = 0, .binding = 0,
                .logical_shape = &.{}, .elem = .f32, .read_write = false}},
            .reductions = &.{}, .collectives = &.{.{.kind = .subgroup_add,
                .axis = -1, .dtype = .f32, .exactness = exactness}},
            .body = .{.rms_norm = .{.formula = .sum_squares_mean_epsilon_rsqrt_scale,
                .epsilon = .{.source = .uniform_field, .path = text},
                .hidden_extent_axis = 0, .reduction_target = .intermediate_scalar}},
            .source_digest = zero}}};
    const realization = s.Realization{.emitter_digest = zero, .rejections = &rejection,
        .functions = &.{.{.semantic_index = 0, .tiles = .{.per_axis = &.{}},
            .pe_grid = .{.width = 0, .height = 0}, .residency = &.{}, .collectives = &.{},
            .emitter_params_json = text, .target_descriptor_hash = zero}}};
    const digests = try d.compute(std.heap.page_allocator, semantic, realization, text);
    const entry = s.ManifestLoweringEntry{.kernel_ref = text, .backend = text,
        .frontend_version = text, .compiler_version = text, .exactness = exactness,
        .target_descriptor_correctness_hash = zero, .tsir_semantic_digest = zero,
        .tsir_realization_digest = zero, .emitter_digest = zero, .rejection_reasons = &.{}};
    var descriptor = t.webgpu_generic.descriptor;
    descriptor.correctness.name = text;
    descriptor.correctness.native_collectives = &.{.{.kind_name = text, .exactness_name = text}};
    const manifest = try d.canonicalizeManifestLoweringEntry(std.heap.page_allocator, entry);
    defer std.heap.page_allocator.free(manifest);
    var manifest_hash: [32]u8 = undefined;
    std.crypto.hash.sha2.Sha256.hash(manifest, &manifest_hash, .{});
    var realized = realization.functions[0];
    realized.target_descriptor_hash = t.descriptorHash(descriptor);
    const emitted = try skeleton.emitFunction(std.heap.page_allocator, realized, descriptor,
        .{.version_key = text, .body_comment = text, .backend = .dxil});
    defer std.heap.page_allocator.free(emitted);
    var skeleton_hash: [32]u8 = undefined;
    std.crypto.hash.sha2.Sha256.hash(emitted, &skeleton_hash, .{});
    const stdout = std.fs.File.stdout().deprecatedWriter();
    for ([_][32]u8{digests.semantic, digests.realization, digests.emitter,
        manifest_hash, t.descriptorHash(descriptor), skeleton_hash}) |hash| {
        for (hash) |byte| try stdout.print("{x:0>2}", .{byte});
        try stdout.writeAll("\n");
    }
}
''')
            source.flush()
            expected = run([zig, "run", "--cache-dir", str(temp / "cache"),
                            "--global-cache-dir", str(temp / "global"), source.name]).splitlines()
        assert actual == expected, (actual, expected)
        assert len(actual) == 6
        print("text spans: 6 actual Zig embedded-NUL semantic/realization/emitter/manifest/target/skeleton identities match")


if __name__ == "__main__":
    main()
