// Exact integer dot reductions for source targets whose dot intrinsic is floating point.
const std = @import("std");
const ir = @import("../ir/ir.zig");

pub fn emitHelpers(self: anytype, metal: bool) !void {
    var used = [_][3]bool{[_]bool{false} ** 3} ** 2;
    for (self.module.functions.items) |function| {
        for (function.exprs.items) |expr| {
            if (expr.data != .call) continue;
            const call = expr.data.call;
            if (call.kind != .builtin or !std.mem.eql(u8, call.name, "dot") or call.args.len != 2) continue;
            const scalar = self.module.types.get(expr.ty);
            if (scalar != .scalar or (scalar.scalar != .i32 and scalar.scalar != .u32)) continue;
            const vector = self.module.types.get(function.exprs.items[function.expr_args.items[call.args.start]].ty);
            if (vector != .vector or vector.vector.len < 2 or vector.vector.len > 4) return error.InvalidIr;
            used[@intFromBool(scalar.scalar == .i32)][vector.vector.len - 2] = true;
        }
    }
    for (used, 0..) |lengths, signed| {
        for (lengths, 0..) |needed, index| {
            if (!needed) continue;
            const scalar = if (signed == 1) "int" else "uint";
            const len: u32 = @intCast(index + 2);
            try self.write(scalar);
            try self.write(" __doe_integer_dot_");
            try self.write(if (signed == 1) "i" else "u");
            try self.write_u32(len);
            try self.write("(");
            try self.write(scalar);
            try self.write_u32(len);
            try self.write(" a, ");
            try self.write(scalar);
            try self.write_u32(len);
            try self.write(" b) { uint sum = ");
            for (0..len) |lane| {
                if (lane != 0) try self.write(" + ");
                const component = "xyzw"[lane .. lane + 1];
                try self.write("uint(a.");
                try self.write(component);
                try self.write(") * uint(b.");
                try self.write(component);
                try self.write(")");
            }
            try self.write("; return ");
            if (signed == 1) try self.write(if (metal) "as_type<int>(sum)" else "asint(sum)") else try self.write("sum");
            try self.write("; }\n");
        }
    }
}

pub fn emitCall(self: anytype, function: ir.Function, result_ty: ir.TypeId, args: ir.Range) !bool {
    const scalar = self.module.types.get(result_ty);
    if (scalar != .scalar or (scalar.scalar != .i32 and scalar.scalar != .u32)) return false;
    if (args.len != 2) return error.InvalidIr;
    const vector = self.module.types.get(function.exprs.items[function.expr_args.items[args.start]].ty);
    if (vector != .vector or vector.vector.len < 2 or vector.vector.len > 4) return error.InvalidIr;
    try self.write("__doe_integer_dot_");
    try self.write(if (scalar.scalar == .i32) "i" else "u");
    try self.write_u32(vector.vector.len);
    try self.write("(");
    try self.emit_expr(function, function.expr_args.items[args.start]);
    try self.write(", ");
    try self.emit_expr(function, function.expr_args.items[args.start + 1]);
    try self.write(")");
    return true;
}
