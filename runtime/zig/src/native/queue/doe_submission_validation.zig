const std = @import("std");
const leases = @import("../../contracts/resource_lease.zig");
const objects = @import("../support/doe_native_object_types.zig");
const helpers = @import("../support/doe_native_object_helpers.zig");
const queries = @import("../resource/doe_query_native.zig");
const external = @import("../resource/doe_external_texture_native.zig");
const bundles = @import("../../runtime/render/render_bundle.zig");

pub const ValidationError = error{
    InvalidReference,
    CommandBufferUnavailable,
    CommandBufferDeviceMismatch,
    BufferUnavailable,
    BufferMapped,
    TextureUnavailable,
    ExternalTextureExpired,
    QueryDestroyed,
    InvalidBundle,
    PipelineUnavailable,
};

pub fn message(err: ValidationError) []const u8 {
    return switch (err) {
        error.CommandBufferUnavailable => "queue submission requires valid command buffers that have not been submitted",
        error.CommandBufferDeviceMismatch => "queue submission requires command buffers from the queue device",
        error.InvalidReference => "queue submission requires a valid retained resource",
        error.BufferUnavailable => "queue submission uses an invalid or destroyed buffer",
        error.BufferMapped => "queue submission requires buffers to be unmapped",
        error.TextureUnavailable => "queue submission uses an invalid or destroyed texture",
        error.ExternalTextureExpired => "queue submission uses an expired external texture",
        error.QueryDestroyed => "queue submission uses a destroyed query set",
        error.InvalidBundle => "queue submission uses an invalid render bundle",
        error.PipelineUnavailable => "queue submission uses a pipeline from a lost device",
    };
}

// Submission consumes every supplied command buffer, including on rejection.
// Mark while validating to detect duplicates without allocation or quadratic
// searches; do not return early and leave a later buffer reusable after failure.
pub fn consumeCommandBuffers(device: *objects.DoeDevice, raw_buffers: []const ?*anyopaque) ValidationError!void {
    var failure: ?ValidationError = null;
    for (raw_buffers) |raw| {
        const buffer = object(objects.DoeCommandBuffer, raw) catch |err| {
            failure = failure orelse err;
            continue;
        };
        const already_submitted = @atomicRmw(bool, &buffer.submitted, .Xchg, true, .acq_rel);
        if (already_submitted or buffer.error_object) {
            failure = failure orelse error.CommandBufferUnavailable;
            continue;
        }
        if (buffer.dev != device) {
            failure = failure orelse error.CommandBufferDeviceMismatch;
            continue;
        }
        validate(buffer.references.items) catch |err| {
            failure = failure orelse err;
        };
    }
    if (failure) |err| return err;
}

pub fn validate(references: []const leases.ResourceLease) ValidationError!void {
    for (references) |reference| switch (reference.kind) {
        .buffer => try validateBuffer(try object(objects.DoeBuffer, reference.handle)),
        .texture => try validateTexture(try object(objects.DoeTexture, reference.handle)),
        .texture_view => try validateView(try object(objects.DoeTextureView, reference.handle)),
        .bind_group => try validateGroup(try object(objects.DoeBindGroup, reference.handle)),
        .render_bundle => {
            const bundle = try object(bundles.DoeRenderBundle, reference.handle);
            if (bundle.error_object) return error.InvalidBundle;
            try validate(bundle.references.items);
        },
        .query_set => {
            const query = try object(queries.DoeQuerySet, reference.handle);
            if (query.destroyed) return error.QueryDestroyed;
        },
        .compute_pipeline => {
            if ((try object(objects.DoeComputePipeline, reference.handle)).error_object) return error.PipelineUnavailable;
        },
        .render_pipeline => {
            if ((try object(objects.DoeRenderPipeline, reference.handle)).error_object) return error.PipelineUnavailable;
        },
        .device => _ = try object(objects.DoeDevice, reference.handle),
        .untyped => return error.InvalidReference,
    };
}

fn object(comptime T: type, raw: ?*anyopaque) ValidationError!*T {
    return helpers.cast(T, raw) orelse error.InvalidReference;
}

fn validateBuffer(buffer: *const objects.DoeBuffer) ValidationError!void {
    if (buffer.error_object or buffer.destroyed) return error.BufferUnavailable;
    if (buffer.mapped) return error.BufferMapped;
}

fn validateTexture(texture: *const objects.DoeTexture) ValidationError!void {
    if (texture.isUnavailable()) return error.TextureUnavailable;
}

fn validateView(view: *const objects.DoeTextureView) ValidationError!void {
    try validateTexture(view.tex);
}

fn validateGroup(group: *const objects.DoeBindGroup) ValidationError!void {
    for (group.retained_buffers) |buffer| if (buffer) |value| try validateBuffer(value);
    for (group.retained_texture_views) |view| if (view) |value| try validateView(value);
    for (group.render_texture_views[0..group.render_texture_view_count]) |view| {
        try validateView(view orelse return error.InvalidReference);
    }
    for (group.retained_external_textures) |raw| if (raw != null) {
        const texture = try object(external.DoeExternalTexture, raw);
        if (texture.expired) return error.ExternalTextureExpired;
        if (!texture.native_imported) {
            try validateView(try object(objects.DoeTextureView, texture.plane0));
            if (!texture.is_single_plane) try validateView(try object(objects.DoeTextureView, texture.plane1));
        }
    };
}

fn testReference(kind: leases.ResourceLease.Kind, raw: ?*anyopaque) leases.ResourceLease {
    return .{ .kind = kind, .handle = raw, .release = struct {
        fn unused(_: ?*anyopaque) callconv(.c) void {}
    }.unused };
}

test "submission rechecks mutable resources through bind groups and bundles without taking ownership" {
    var buffer: objects.DoeBuffer = .{ .ref_count = 2 };
    var texture: objects.DoeTexture = .{};
    var view: objects.DoeTextureView = .{ .tex = &texture };
    var group: objects.DoeBindGroup = .{};
    group.retained_buffers[0] = &buffer;
    group.render_texture_views[0] = &view;
    group.render_texture_view_count = 1;
    var external_texture: external.DoeExternalTexture = .{ .plane0 = &view };
    group.retained_external_textures[1] = @ptrCast(&external_texture);
    var group_references = [_]leases.ResourceLease{testReference(.bind_group, &group)};
    var bundle: bundles.DoeRenderBundle = .{
        .allocator = std.testing.allocator,
        .color_format = 0,
        .depth_stencil_format = 0,
        .sample_count = 1,
        .cmds = &.{},
        .references = .{ .items = &group_references, .capacity = group_references.len },
    };
    const references = [_]leases.ResourceLease{testReference(.render_bundle, &bundle)};
    try validate(&references);
    buffer.mapped = true;
    try std.testing.expectError(error.BufferMapped, validate(&references));
    buffer.mapped = false;
    buffer.destroyed = true;
    try std.testing.expectError(error.BufferUnavailable, validate(&references));
    buffer.destroyed = false;
    buffer.error_object = true;
    try std.testing.expectError(error.BufferUnavailable, validate(&references));
    buffer.error_object = false;
    texture.destroyed = true;
    try std.testing.expectError(error.TextureUnavailable, validate(&references));
    texture.destroyed = false;
    external_texture.expired = true;
    try std.testing.expectError(error.ExternalTextureExpired, validate(&references));
    external_texture.expired = false;
    bundle.error_object = true;
    try std.testing.expectError(error.InvalidBundle, validate(&references));
    bundle.error_object = false;
    try validate(&references);
    try std.testing.expectEqual(@as(u32, 2), buffer.ref_count);
}

test "submission validates direct leases and rejects destroyed queries and untyped references" {
    var query: queries.DoeQuerySet = .{};
    const references = [_]leases.ResourceLease{testReference(.query_set, &query)};
    try validate(&references);
    query.destroyed = true;
    try std.testing.expectError(error.QueryDestroyed, validate(&references));
    try std.testing.expectError(error.InvalidReference, validate(&.{testReference(.untyped, &query)}));
    try std.testing.expectError(error.InvalidReference, validate(&.{testReference(.buffer, null)}));
    var buffer: objects.DoeBuffer = .{ .mapped = true };
    try std.testing.expectError(error.BufferMapped, validate(&.{testReference(.buffer, &buffer)}));
    var texture: objects.DoeTexture = .{ .destroyed = true };
    try std.testing.expectError(error.TextureUnavailable, validate(&.{testReference(.texture, &texture)}));
}

test "lost pipeline objects cannot enter command submission" {
    var compute = objects.DoeComputePipeline{ .error_object = true };
    var render = objects.DoeRenderPipeline{ .error_object = true };
    const compute_references = [_]leases.ResourceLease{testReference(.compute_pipeline, &compute)};
    const render_references = [_]leases.ResourceLease{testReference(.render_pipeline, &render)};
    try std.testing.expectError(error.PipelineUnavailable, validate(&compute_references));
    try std.testing.expectError(error.PipelineUnavailable, validate(&render_references));
}

test "submission consumes command buffers exactly once and rejects duplicates atomically" {
    var device: objects.DoeDevice = .{};
    var first: objects.DoeCommandBuffer = .{ .allocator = std.testing.allocator, .dev = &device };
    var second: objects.DoeCommandBuffer = .{ .allocator = std.testing.allocator, .dev = &device };
    try consumeCommandBuffers(&device, &.{&first});
    try std.testing.expectError(error.CommandBufferUnavailable, consumeCommandBuffers(&device, &.{&first}));
    try std.testing.expectError(error.CommandBufferUnavailable, consumeCommandBuffers(&device, &.{ &second, &second }));
    try std.testing.expect(first.submitted and second.submitted);
    try std.testing.expectEqual(@as(u32, 1), first.ref_count);
    try std.testing.expectEqual(@as(u32, 1), second.ref_count);
}

test "submission consumes all valid handles even when earlier batch entries fail" {
    var device: objects.DoeDevice = .{};
    var other: objects.DoeDevice = .{};
    var first: objects.DoeCommandBuffer = .{ .allocator = std.testing.allocator, .dev = &device };
    var last: objects.DoeCommandBuffer = .{ .allocator = std.testing.allocator, .dev = &device };
    try std.testing.expectError(error.InvalidReference, consumeCommandBuffers(&device, &.{ null, &first, &last }));
    try std.testing.expect(first.submitted and last.submitted);
    var foreign: objects.DoeCommandBuffer = .{ .allocator = std.testing.allocator, .dev = &other };
    var fresh: objects.DoeCommandBuffer = .{ .allocator = std.testing.allocator, .dev = &device };
    try std.testing.expectError(error.CommandBufferDeviceMismatch, consumeCommandBuffers(&device, &.{ &foreign, &fresh }));
    try std.testing.expect(foreign.submitted and fresh.submitted);
    var recovery: objects.DoeCommandBuffer = .{ .allocator = std.testing.allocator, .dev = &device };
    try consumeCommandBuffers(&device, &.{&recovery});
}

test "submission consumes command buffers rejected for unavailable retained resources" {
    var device: objects.DoeDevice = .{};
    var buffer: objects.DoeBuffer = .{ .mapped = true };
    var refs = [_]leases.ResourceLease{testReference(.buffer, &buffer)};
    var commands: objects.DoeCommandBuffer = .{ .allocator = std.testing.allocator, .dev = &device, .references = .{ .items = &refs, .capacity = refs.len } };
    try std.testing.expectError(error.BufferMapped, consumeCommandBuffers(&device, &.{&commands}));
    buffer.mapped = false;
    try std.testing.expectError(error.CommandBufferUnavailable, consumeCommandBuffers(&device, &.{&commands}));
    try std.testing.expectEqual(@as(u32, 1), buffer.ref_count);
}
