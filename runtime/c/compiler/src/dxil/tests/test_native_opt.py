#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compare projected C optimizer results with the actual corrected Zig owner.

The retained owner was deliberately corrected for side effects and modulo-32
runtime shifts; this is not an unchanged-reference or physical GPU claim.
"""
from pathlib import Path
import argparse
import os
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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--c-driver", type=Path, default=ROOT / "runtime/c/compiler/build/test_dxil_native_opt")
    parser.add_argument("--zig", default=os.environ.get("ZIG", "zig"))
    args = parser.parse_args()
    zig = shutil.which(args.zig)
    assert zig and run([zig, "version"]).strip() == "0.15.2", "pinned Zig 0.15.2 required"
    print(run([str(args.c_driver)]), end="")
    with tempfile.TemporaryDirectory(prefix="native-opt-") as directory:
        temp = Path(directory)
        with tempfile.NamedTemporaryFile(mode="w", suffix=".zig", prefix="native_opt_oracle_",
                                         dir=ROOT / "runtime/zig/src") as source:
            source.write(r'''const std = @import("std");
const ir = @import("compiler/wgsl/ir/ir.zig");
const opt = @import("compiler/wgsl/ir/ir_opt_rewrite.zig");
pub fn main() !void {
    var arena = std.heap.ArenaAllocator.init(std.heap.page_allocator);
    defer arena.deinit();
    const a = arena.allocator();
    const args = try std.process.argsAlloc(a);
    if (args.len != 2) return error.InvalidArguments;
    const which = try std.fmt.parseInt(u32, args[1], 10);
    if (which >= 16) return error.InvalidArguments;
    var m = ir.Module.init(a);
    const ty = try m.types.intern(.{ .scalar = if (which == 4 or which == 5) .bool
        else if (which == 10 or which == 12 or which == 14) .i32 else .u32 });
    var f = ir.Function{ .name = try ir.dup_string(a, "probe"), .return_type = ty };
    const x = try f.append_expr(a, .{ .ty = ty, .category = .value, .data = .{ .int_lit = 64 } });
    const y = try f.append_expr(a, .{ .ty = ty, .category = .value, .data = .{ .int_lit = 2 } });
    const result = try f.append_expr(a, .{ .ty = ty, .category = .value,
        .data = .{ .binary = .{ .op = .mul, .lhs = x, .rhs = y } } });
    if (which == 0 or which == 7) {
        try f.locals.append(a, .{ .name = try ir.dup_string(a, "alias"), .ty = ty, .mutable = which == 7 });
        _ = try f.append_stmt(a, .{ .local_decl = .{ .local = 0, .initializer = x, .is_const = which == 0 } });
        const alias = try f.append_expr(a, .{ .ty = ty, .category = .value, .data = .{ .local_ref = 0 } });
        f.exprs.items[result].data.binary.lhs = alias;
    } else if (which == 1) {
        try m.globals.append(a, .{ .name = try ir.dup_string(a, "global"), .ty = ty,
            .class = .const_, .initializer = .{ .int = 7 } });
        f.exprs.items[x].data = .{ .global_ref = 0 };
        f.exprs.items[y].data = .{ .int_lit = 4 };
        f.exprs.items[result].data.binary.op = .add;
    } else if (which >= 2 and which <= 6) {
        f.exprs.items[x].data = if (which == 6) .{ .call = .{ .name = try ir.dup_string(a, "effect"),
            .kind = .user, .args = .{ .start = 0, .len = 0 } } } else .{ .param_ref = 0 };
        f.exprs.items[y].data = .{ .int_lit = if (which == 3) 1 else 0 };
        f.exprs.items[result].data.binary.op = if (which == 2) .add else .mul;
        if (which == 4 or which == 5) {
            f.exprs.items[y].data = .{ .bool_lit = which == 4 };
            f.exprs.items[result].data.binary.op = if (which == 4) .logical_and else .logical_or;
        }
    } else if (which == 8) {
        f.exprs.items[x].data = .{ .param_ref = 0 };
        f.exprs.items[x].category = .ref;
        f.exprs.items[y].data = .{ .int_lit = 0 };
        f.exprs.items[result].data.binary.op = .add;
    } else if (which == 9 or which == 10) {
        f.exprs.items[x].data = .{ .int_lit = if (which == 10) 0x80000000 else 7 };
        f.exprs.items[y].data = .{ .int_lit = if (which == 10) 0xffffffff else 0 };
        f.exprs.items[result].data.binary.op = .div;
    } else if (which == 11) {
        f.exprs.items[x].data = .{ .int_lit = 0xffffffff };
        f.exprs.items[result].data.binary.op = .add;
    } else if (which == 12) {
        f.exprs.items[x].data = .{ .int_lit = 0x80000000 };
        f.exprs.items[result].data = .{ .unary = .{ .op = .neg, .operand = x } };
    } else {
        f.exprs.items[x].data = .{ .int_lit = if (which == 13) 7 else 0x80000000 };
        f.exprs.items[y].ty = try m.types.intern(.{ .scalar = .u32 });
        f.exprs.items[y].data = .{ .int_lit = if (which == 13) 32 else if (which == 14) 33 else 1 };
        f.exprs.items[result].data.binary.op = if (which == 14) .shift_right else .shift_left;
    }
    f.root_stmt = try f.append_stmt(a, .{ .return_ = result });
    try m.functions.append(a, f);
    _ = try opt.apply(a, &m);
    const id = m.functions.items[0].stmts.items[f.root_stmt].return_.?;
    const value = m.functions.items[0].exprs.items[id].data;
    try std.fs.File.stdout().deprecatedWriter().print("{s} {d}
", .{
        @tagName(value), if (value == .int_lit) value.int_lit else @as(u64, 0) });
}
'''.replace('"{s} {d}\n"', '"{s} {d}\\n"'))
            source.flush()
            binary = temp / "oracle"
            run([zig, "build-exe", source.name, "-O", "Debug", "-lc",
                 "--cache-dir", str(temp / "cache"), "--global-cache-dir", str(temp / "global"),
                 "-femit-bin=" + str(binary)])
            for case in range(16):
                actual = run([str(args.c_driver), "--case", str(case)])
                expected = run([str(binary), str(case)])
                assert actual == expected, (case, actual, expected)
    print("Native optimizer differential: 16 actual corrected Zig typed-IR results match")


if __name__ == "__main__":
    main()
