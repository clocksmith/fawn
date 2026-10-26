#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compare caller-built C and retained Zig HLSL numeric serialization owners."""
from pathlib import Path
import argparse
import os
import re
import shutil
import signal
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[6]


def run(args):
    child = subprocess.Popen(args, cwd=ROOT, stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE, start_new_session=True)
    try:
        stdout, stderr = child.communicate(timeout=120)
    except subprocess.TimeoutExpired:
        os.killpg(child.pid, signal.SIGKILL)
        child.communicate()
        raise
    assert child.returncode == 0, (args, child.returncode, stderr.decode(errors="replace"))
    return stdout.decode()


def relevant(text):
    function = re.search(r"^(?:uint|int|float) numeric\([^\n]*\) \{\n.*?^\}",
                         text, re.M | re.S)
    assert function is not None, text
    helpers = re.findall(r"^(?:uint|int) __doe_integer_dot_[iu][234]\([^\n]+", text, re.M)
    return function.group(), helpers


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--c-driver", type=Path, default=ROOT / "runtime/c/compiler/build/test_dxil_hlsl_numeric")
    parser.add_argument("--zig", default=os.environ.get("ZIG", "zig"))
    args = parser.parse_args()
    zig = shutil.which(args.zig)
    assert zig and run([zig, "version"]).strip() == "0.15.2", "pinned Zig 0.15.2 required"
    print(run([str(args.c_driver)]), end="")
    with tempfile.TemporaryDirectory(prefix="hlsl-numeric-") as directory:
        temp = Path(directory)
        with tempfile.NamedTemporaryFile(mode="w", suffix=".zig", prefix="hlsl_numeric_oracle_",
                                         dir=ROOT / "runtime/zig/src") as source:
            source.write(r'''const std = @import("std");
const ir = @import("compiler/wgsl/ir/ir.zig");
const hlsl = @import("compiler/wgsl/emit/hlsl/emit_hlsl.zig");
pub fn main() !void {
    var arena = std.heap.ArenaAllocator.init(std.heap.page_allocator);
    defer arena.deinit();
    const allocator = arena.allocator();
    const args = try std.process.argsAlloc(allocator);
    if (args.len != 2) return error.InvalidArguments;
    const which = try std.fmt.parseInt(u32, args[1], 10);
    if (which > 10) return error.InvalidArguments;
    var module = ir.Module.init(allocator);
    const scalar = try module.types.intern(.{ .scalar = if (which < 2 or (which >= 4 and which < 7)) .u32
        else if (which >= 7 and which < 10) .i32 else .f32 });
    const argument_type = if (which < 4) scalar else try module.types.intern(.{ .vector = .{
        .elem = scalar, .len = @intCast(if (which == 10) 2 else (which - 4) % 3 + 2) } });
    const count: u32 = if (which == 0) 3 else if (which == 1) 4 else if (which < 4) 1 else 2;
    var function = ir.Function{ .name = try ir.dup_string(allocator, "numeric"), .return_type = scalar };
    const names = [_][]const u8{"a", "b", "c", "d"};
    for (0..count) |i| {
        try function.params.append(allocator, .{ .name = try ir.dup_string(allocator, names[i]), .ty = argument_type });
        const ref = try function.append_expr(allocator, .{ .ty = argument_type, .category = .ref,
            .data = .{ .param_ref = @intCast(i) } });
        const value = try function.append_expr(allocator, .{ .ty = argument_type, .category = .value,
            .data = .{ .load = ref } });
        try function.expr_args.append(allocator, value);
    }
    const builtins = [_][]const u8{"extractBits", "insertBits", "degrees", "radians"};
    const call = try function.append_expr(allocator, .{ .ty = scalar, .category = .value,
        .data = .{ .call = .{ .name = try ir.dup_string(allocator, if (which < 4) builtins[which] else "dot"),
            .kind = .builtin, .args = .{ .start = 0, .len = count } } } });
    const result = try function.append_stmt(allocator, .{ .return_ = call });
    const children = try function.append_stmt_children(allocator, &.{result});
    function.root_stmt = try function.append_stmt(allocator, .{ .block = children });
    try module.functions.append(allocator, function);
    var output: [65536]u8 = undefined;
    const length = try hlsl.emit(&module, &output);
    try std.fs.File.stdout().deprecatedWriter().writeAll(output[0..length]);
}
''')
            source.flush()
            binary = temp / "oracle"
            run([zig, "build-exe", source.name, "-O", "Debug", "-lc",
                 "--cache-dir", str(temp / "cache"), "--global-cache-dir", str(temp / "global"),
                 "-femit-bin=" + str(binary)])
            for case in range(11):
                actual = relevant(run([str(args.c_driver), "--case", str(case)]))
                expected = relevant(run([str(binary), str(case)]))
                assert actual == expected, (case, actual, expected)
    print("HLSL numeric differential: 11 retained Zig typed-IR functions and selected integer helpers match")


if __name__ == "__main__":
    main()
