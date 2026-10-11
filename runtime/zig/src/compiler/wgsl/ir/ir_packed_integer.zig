// Packed 4x8 integer semantics shared by frontend admission and typed lowering.
const std = @import("std");
const ir = @import("ir.zig");

pub const Operation = enum {
    pack4xI8,
    pack4xU8,
    pack4xI8Clamp,
    pack4xU8Clamp,
    unpack4xI8,
    unpack4xU8,
    dot4I8Packed,
    dot4U8Packed,

    pub fn isSigned(self: Operation) bool {
        return switch (self) {
            .pack4xI8, .pack4xI8Clamp, .unpack4xI8, .dot4I8Packed => true,
            else => false,
        };
    }

    pub fn isPack(self: Operation) bool {
        return switch (self) {
            .pack4xI8, .pack4xU8, .pack4xI8Clamp, .pack4xU8Clamp => true,
            else => false,
        };
    }

    pub fn isDot(self: Operation) bool {
        return self == .dot4I8Packed or self == .dot4U8Packed;
    }
};

pub fn lookup(name: []const u8) ?Operation {
    return std.meta.stringToEnum(Operation, name);
}

pub fn inferType(op: Operation, types: *ir.TypeStore, u32_ty: ir.TypeId, i32_ty: ir.TypeId, abstract_ty: ir.TypeId, args: []const ir.TypeId) !ir.TypeId {
    if (args.len != @as(usize, if (op.isDot()) 2 else 1)) return error.UnsupportedBuiltin;
    const elem = if (op.isSigned()) i32_ty else u32_ty;
    // Lowering needs both signed and unsigned vectors, including for bitcasts.
    const unsigned_vector = try types.intern(.{ .vector = .{ .elem = u32_ty, .len = 4 } });
    const signed_vector = try types.intern(.{ .vector = .{ .elem = i32_ty, .len = 4 } });
    if (op.isPack()) {
        const vector = switch (types.get(args[0])) {
            .vector => |value| value,
            else => return error.TypeMismatch,
        };
        if (vector.len != 4 or (vector.elem != elem and vector.elem != abstract_ty)) return error.TypeMismatch;
        return u32_ty;
    }
    for (args) |arg| if (arg != u32_ty and arg != abstract_ty) return error.TypeMismatch;
    return if (op.isDot()) elem else if (op.isSigned()) signed_vector else unsigned_vector;
}

pub const Lowering = struct {
    allocator: std.mem.Allocator,
    function: *ir.Function,
    u32_ty: ir.TypeId,
    i32_ty: ir.TypeId,
    vec_u32_ty: ir.TypeId,
    vec_i32_ty: ir.TypeId,

    pub fn lower(self: Lowering, op: Operation, args: []const ir.ExprId) !ir.Expr {
        if (op.isPack()) {
            var value = args[0];
            const input_ty = if (op.isSigned()) self.vec_i32_ty else self.vec_u32_ty;
            if (op == .pack4xI8Clamp or op == .pack4xU8Clamp) {
                const lower_bound = if (op.isSigned()) @as(u64, @bitCast(@as(i64, -128))) else 0;
                const upper_bound: u64 = if (op.isSigned()) 127 else 255;
                const elem_ty = if (op.isSigned()) self.i32_ty else self.u32_ty;
                value = try self.call(input_ty, "clamp", &.{ value, try self.splat(input_ty, try self.literal(elem_ty, lower_bound)), try self.splat(input_ty, try self.literal(elem_ty, upper_bound)) });
            }
            if (op.isSigned()) value = try self.call(self.vec_u32_ty, "bitcast", &.{value});
            const masked = try self.binary(self.vec_u32_ty, .bit_and, value, try self.splat(self.vec_u32_ty, try self.literal(self.u32_ty, 255)));
            const shifted = try self.binary(self.vec_u32_ty, .shift_left, masked, try self.offsets(false));
            // Disjoint bytes can be summed. The typed integer reduction keeps
            // its vector input single-evaluated in every target emitter.
            const range = try self.function.append_expr_args(self.allocator, &.{ shifted, try self.splat(self.vec_u32_ty, try self.literal(self.u32_ty, 1)) });
            return .{ .call = .{ .name = try ir.dup_string(self.allocator, "dot"), .kind = .builtin, .args = range } };
        }
        const left = try self.unpack(args[0], op.isSigned());
        if (!op.isDot()) return self.function.exprs.items[left].data;
        const right = try self.unpack(args[1], op.isSigned());
        const range = try self.function.append_expr_args(self.allocator, &.{ left, right });
        return .{ .call = .{ .name = try ir.dup_string(self.allocator, "dot"), .kind = .builtin, .args = range } };
    }

    fn unpack(self: Lowering, arg: ir.ExprId, signed: bool) !ir.ExprId {
        const value = try self.splat(self.vec_u32_ty, arg);
        if (signed) {
            const high_bytes = try self.binary(self.vec_u32_ty, .shift_left, value, try self.offsets(true));
            const signed_bytes = try self.call(self.vec_i32_ty, "bitcast", &.{high_bytes});
            return self.binary(self.vec_i32_ty, .shift_right, signed_bytes, try self.splat(self.vec_u32_ty, try self.literal(self.u32_ty, 24)));
        }
        const shifted = try self.binary(self.vec_u32_ty, .shift_right, value, try self.offsets(false));
        return self.binary(self.vec_u32_ty, .bit_and, shifted, try self.splat(self.vec_u32_ty, try self.literal(self.u32_ty, 255)));
    }

    fn literal(self: Lowering, ty: ir.TypeId, value: u64) !ir.ExprId {
        if (ty == self.i32_ty and @as(i64, @bitCast(value)) < 0) {
            const magnitude = try self.append(ty, .{ .int_lit = (~value) +% 1 });
            return self.append(ty, .{ .unary = .{ .op = .neg, .operand = magnitude } });
        }
        return self.append(ty, .{ .int_lit = value });
    }

    fn splat(self: Lowering, ty: ir.TypeId, value: ir.ExprId) !ir.ExprId {
        return self.construct(ty, &.{value});
    }

    fn offsets(self: Lowering, reverse: bool) !ir.ExprId {
        var values: [4]ir.ExprId = undefined;
        for (&values, 0..) |*value, lane| value.* = try self.literal(self.u32_ty, (if (reverse) 3 - lane else lane) * @bitSizeOf(u8));
        return self.construct(self.vec_u32_ty, &values);
    }

    fn construct(self: Lowering, ty: ir.TypeId, args: []const ir.ExprId) !ir.ExprId {
        return self.append(ty, .{ .construct = .{ .ty = ty, .args = try self.function.append_expr_args(self.allocator, args) } });
    }

    fn binary(self: Lowering, ty: ir.TypeId, op: ir.BinaryOp, lhs: ir.ExprId, rhs: ir.ExprId) !ir.ExprId {
        return self.append(ty, .{ .binary = .{ .op = op, .lhs = lhs, .rhs = rhs } });
    }

    fn call(self: Lowering, ty: ir.TypeId, name: []const u8, args: []const ir.ExprId) !ir.ExprId {
        const owned_name = try ir.dup_string(self.allocator, name);
        errdefer self.allocator.free(owned_name);
        return self.append(ty, .{ .call = .{ .name = owned_name, .kind = .builtin, .args = try self.function.append_expr_args(self.allocator, args) } });
    }

    fn append(self: Lowering, ty: ir.TypeId, expr: ir.Expr) !ir.ExprId {
        return self.function.append_expr(self.allocator, .{ .ty = ty, .category = .value, .data = expr });
    }
};

/// Operands are borrowed; the caller owns the returned constant.
pub fn fold(allocator: std.mem.Allocator, op: Operation, args: []const ir.ConstantValue) !ir.ConstantValue {
    if (args.len != @as(usize, if (op.isDot()) 2 else 1)) return error.InvalidIr;
    if (op.isPack()) {
        const values = switch (args[0]) {
            .composite => |items| items,
            else => return error.InvalidIr,
        };
        if (values.len != 4) return error.InvalidIr;
        var packed_word: u32 = 0;
        for (values, 0..) |value, lane| {
            if (value != .int) return error.InvalidIr;
            var bits: u32 = @truncate(value.int);
            if (op == .pack4xI8Clamp) bits = @bitCast(std.math.clamp(@as(i32, @bitCast(bits)), -128, 127));
            if (op == .pack4xU8Clamp) bits = @min(bits, 255);
            packed_word |= (bits & 255) << @as(u5, @intCast(lane * 8));
        }
        return .{ .int = packed_word };
    }
    for (args) |value| if (value != .int or value.int > std.math.maxInt(u32)) return error.InvalidIr;
    const left: u32 = @intCast(args[0].int);
    if (op.isDot()) {
        const right: u32 = @intCast(args[1].int);
        var sum: i64 = 0;
        for (0..4) |lane| sum += byte(left, lane, op.isSigned()) * byte(right, lane, op.isSigned());
        return .{ .int = if (op.isSigned()) @as(u32, @bitCast(@as(i32, @intCast(sum)))) else @as(u32, @intCast(sum)) };
    }
    const values = try allocator.alloc(ir.ConstantValue, 4);
    for (values, 0..) |*value, lane| value.* = .{ .int = if (op.isSigned()) @as(u32, @bitCast(@as(i32, @intCast(byte(left, lane, true))))) else @as(u32, @intCast(byte(left, lane, false))) };
    return .{ .composite = values };
}

fn byte(word: u32, lane: usize, signed: bool) i64 {
    const bits: u8 = @truncate(word >> @as(u5, @intCast(lane * 8)));
    return if (signed) @as(i8, @bitCast(bits)) else bits;
}
