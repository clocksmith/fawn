const std = @import("std");
const ir = @import("../ir/ir.zig");

// Continuing clauses can be emitted at a continue site inside a shadowing scope.
// Preserve IR identity for colliding locals instead of resolving their source names again.
pub fn local_name(module: *const ir.Module, function: ir.Function, index: u32, buffer: *[32]u8) error{OutputTooLarge}![]const u8 {
    const name = function.locals.items[index].name;
    var collides = false;
    for (function.locals.items, 0..) |local, other| {
        if (other != index and std.mem.eql(u8, local.name, name)) collides = true;
    }
    for (function.params.items) |param| {
        if (std.mem.eql(u8, param.name, name)) collides = true;
    }
    for (module.globals.items) |global| {
        if (std.mem.eql(u8, global.name, name)) collides = true;
    }
    if (!collides) return name;
    // WGSL reserves double-underscore names, so source identifiers cannot collide.
    return std.fmt.bufPrint(buffer, "__doe_local_{d}", .{index}) catch error.OutputTooLarge;
}
