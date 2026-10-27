// webgpu_ffi_test.zig — ABI contract tests for the FFI boundary layer.
//
// These tests catch ABI regressions in the types and enums that cross the
// Zig/native-addon boundary. If any sizeof, alignof, field offset, or enum
// tag value changes, the corresponding test fails immediately.
//
// Tested contracts:
// 1. Type sizes — sizeof key extern structs
// 2. Type alignments — alignof key types
// 3. Field offsets — critical struct field positions
// 4. Enum values — callback/status enums with stable wire values
// 5. Function/decl existence — key exports on WebGPUBackend and ffi module
// 6. Constant values — sentinel/flag constants that cross FFI
// 7. Null-safe defaults — zero-init and default behavior

const std = @import("std");
const types = @import("../../src/core/abi/wgpu_runtime_abi.zig");
const ffi = @import("../../src/compat/webgpu_ffi.zig");
const command = @import("../../src/contracts/command.zig");

// ============================================================
// 1. Type size assertions — ABI break if sizes change
// ============================================================

test "sizeof: WGPUFuture is 8 bytes (single u64)" {
    try std.testing.expectEqual(@as(usize, 8), @sizeOf(types.WGPUFuture));
}

test "sizeof: WGPUStringView is pointer + length" {
    // pointer (8) + usize (8) on 64-bit
    try std.testing.expectEqual(@as(usize, 16), @sizeOf(types.WGPUStringView));
}

test "sizeof: WGPUExtent3D is 3x u32" {
    try std.testing.expectEqual(@as(usize, 12), @sizeOf(types.WGPUExtent3D));
}

test "sizeof: WGPUExtent2D is 2x u32" {
    try std.testing.expectEqual(@as(usize, 8), @sizeOf(types.WGPUExtent2D));
}

test "sizeof: WGPUOrigin3D is 3x u32" {
    try std.testing.expectEqual(@as(usize, 12), @sizeOf(types.WGPUOrigin3D));
}

test "sizeof: WGPUColor is 4x f64" {
    try std.testing.expectEqual(@as(usize, 32), @sizeOf(types.WGPUColor));
}

test "sizeof: WGPUChainedStruct is pointer + u32 sType" {
    // next pointer (8) + sType u32 (4) + padding (4) = 16
    try std.testing.expectEqual(@as(usize, 16), @sizeOf(types.WGPUChainedStruct));
}

test "sizeof: WGPUTexelCopyBufferLayout is u64 + 2x u32" {
    try std.testing.expectEqual(@as(usize, 16), @sizeOf(types.WGPUTexelCopyBufferLayout));
}

test "sizeof: WGPUFutureWaitInfo is WGPUFuture + WGPUBool" {
    // future (8) + completed u32 (4) + padding (4) = 16
    try std.testing.expectEqual(@as(usize, 16), @sizeOf(types.WGPUFutureWaitInfo));
}

test "sizeof: NativeExecutionResult fields are stable" {
    // Must not change without a coordinated FFI update
    const size = @sizeOf(types.NativeExecutionResult);
    // status enum + slice (ptr+len) + 3x u64 timing + u32 dispatch + 2x bool + u64 gpu_ts
    // Exact value depends on padding; pin it to catch drift.
    try std.testing.expect(size > 0);
    try std.testing.expectEqual(@as(usize, 56), size);
}

// ============================================================
// 2. Type alignment assertions — wrong alignment = memory corruption
// ============================================================

test "alignof: WGPUFuture aligns to 8" {
    try std.testing.expectEqual(@as(usize, 8), @alignOf(types.WGPUFuture));
}

test "alignof: WGPUStringView aligns to 8 (pointer alignment)" {
    try std.testing.expectEqual(@as(usize, 8), @alignOf(types.WGPUStringView));
}

test "alignof: WGPUExtent3D aligns to 4" {
    try std.testing.expectEqual(@as(usize, 4), @alignOf(types.WGPUExtent3D));
}

test "alignof: WGPUColor aligns to 8 (f64)" {
    try std.testing.expectEqual(@as(usize, 8), @alignOf(types.WGPUColor));
}

test "alignof: WGPUChainedStruct aligns to 8 (pointer field)" {
    try std.testing.expectEqual(@as(usize, 8), @alignOf(types.WGPUChainedStruct));
}

test "alignof: WGPULimits aligns to 8 (contains u64 and pointer)" {
    try std.testing.expectEqual(@as(usize, 8), @alignOf(types.WGPULimits));
}

test "alignof: WGPUTexelCopyBufferLayout aligns to 8 (leading u64)" {
    try std.testing.expectEqual(@as(usize, 8), @alignOf(types.WGPUTexelCopyBufferLayout));
}

test "alignof: WGPUBufferDescriptor aligns to 8" {
    try std.testing.expectEqual(@as(usize, 8), @alignOf(types.WGPUBufferDescriptor));
}

test "alignof: WGPURenderPassColorAttachment aligns to 8" {
    try std.testing.expectEqual(@as(usize, 8), @alignOf(types.WGPURenderPassColorAttachment));
}

// ============================================================
// 3. Field offset assertions — catch struct layout shifts
// ============================================================

test "field offsets: WGPUStringView" {
    try std.testing.expectEqual(@as(usize, 0), @offsetOf(types.WGPUStringView, "data"));
    try std.testing.expectEqual(@as(usize, 8), @offsetOf(types.WGPUStringView, "length"));
}

test "field offsets: WGPUFuture" {
    try std.testing.expectEqual(@as(usize, 0), @offsetOf(types.WGPUFuture, "id"));
}

test "field offsets: WGPUChainedStruct" {
    try std.testing.expectEqual(@as(usize, 0), @offsetOf(types.WGPUChainedStruct, "next"));
    try std.testing.expectEqual(@as(usize, 8), @offsetOf(types.WGPUChainedStruct, "sType"));
}

test "field offsets: WGPUExtent3D" {
    try std.testing.expectEqual(@as(usize, 0), @offsetOf(types.WGPUExtent3D, "width"));
    try std.testing.expectEqual(@as(usize, 4), @offsetOf(types.WGPUExtent3D, "height"));
    try std.testing.expectEqual(@as(usize, 8), @offsetOf(types.WGPUExtent3D, "depthOrArrayLayers"));
}

test "field offsets: WGPUOrigin3D" {
    try std.testing.expectEqual(@as(usize, 0), @offsetOf(types.WGPUOrigin3D, "x"));
    try std.testing.expectEqual(@as(usize, 4), @offsetOf(types.WGPUOrigin3D, "y"));
    try std.testing.expectEqual(@as(usize, 8), @offsetOf(types.WGPUOrigin3D, "z"));
}

test "field offsets: WGPUTexelCopyBufferLayout" {
    try std.testing.expectEqual(@as(usize, 0), @offsetOf(types.WGPUTexelCopyBufferLayout, "offset"));
    try std.testing.expectEqual(@as(usize, 8), @offsetOf(types.WGPUTexelCopyBufferLayout, "bytesPerRow"));
    try std.testing.expectEqual(@as(usize, 12), @offsetOf(types.WGPUTexelCopyBufferLayout, "rowsPerImage"));
}

test "field offsets: WGPUColor" {
    try std.testing.expectEqual(@as(usize, 0), @offsetOf(types.WGPUColor, "r"));
    try std.testing.expectEqual(@as(usize, 8), @offsetOf(types.WGPUColor, "g"));
    try std.testing.expectEqual(@as(usize, 16), @offsetOf(types.WGPUColor, "b"));
    try std.testing.expectEqual(@as(usize, 24), @offsetOf(types.WGPUColor, "a"));
}

test "field offsets: WGPUFutureWaitInfo" {
    try std.testing.expectEqual(@as(usize, 0), @offsetOf(types.WGPUFutureWaitInfo, "future"));
    try std.testing.expectEqual(@as(usize, 8), @offsetOf(types.WGPUFutureWaitInfo, "completed"));
}

test "field offsets: WGPUBufferDescriptor" {
    try std.testing.expectEqual(@as(usize, 0), @offsetOf(types.WGPUBufferDescriptor, "nextInChain"));
    try std.testing.expectEqual(@as(usize, 8), @offsetOf(types.WGPUBufferDescriptor, "label"));
    try std.testing.expectEqual(@as(usize, 24), @offsetOf(types.WGPUBufferDescriptor, "usage"));
    try std.testing.expectEqual(@as(usize, 32), @offsetOf(types.WGPUBufferDescriptor, "size"));
    try std.testing.expectEqual(@as(usize, 40), @offsetOf(types.WGPUBufferDescriptor, "mappedAtCreation"));
}

test "field offsets: WGPUShaderSourceWGSL" {
    try std.testing.expectEqual(@as(usize, 0), @offsetOf(types.WGPUShaderSourceWGSL, "chain"));
    try std.testing.expectEqual(@as(usize, 16), @offsetOf(types.WGPUShaderSourceWGSL, "code"));
}

test "field offsets: WGPURequestAdapterOptions" {
    try std.testing.expectEqual(@as(usize, 0), @offsetOf(types.WGPURequestAdapterOptions, "nextInChain"));
    try std.testing.expectEqual(@as(usize, 8), @offsetOf(types.WGPURequestAdapterOptions, "featureLevel"));
    try std.testing.expectEqual(@as(usize, 12), @offsetOf(types.WGPURequestAdapterOptions, "powerPreference"));
    try std.testing.expectEqual(@as(usize, 16), @offsetOf(types.WGPURequestAdapterOptions, "forceFallbackAdapter"));
    try std.testing.expectEqual(@as(usize, 20), @offsetOf(types.WGPURequestAdapterOptions, "backendType"));
    try std.testing.expectEqual(@as(usize, 24), @offsetOf(types.WGPURequestAdapterOptions, "compatibleSurface"));
}

test "field offsets: WGPULimits leading fields" {
    try std.testing.expectEqual(@as(usize, 0), @offsetOf(types.WGPULimits, "nextInChain"));
    try std.testing.expectEqual(@as(usize, 8), @offsetOf(types.WGPULimits, "maxTextureDimension1D"));
    try std.testing.expectEqual(@as(usize, 12), @offsetOf(types.WGPULimits, "maxTextureDimension2D"));
    try std.testing.expectEqual(@as(usize, 16), @offsetOf(types.WGPULimits, "maxTextureDimension3D"));
}

// ============================================================
// 4. Enum value assertions — these cross the FFI boundary
// ============================================================

test "enum values: NativeExecutionStatus" {
    try std.testing.expectEqual(@as(u2, 0), @intFromEnum(types.NativeExecutionStatus.ok));
    try std.testing.expectEqual(@as(u2, 1), @intFromEnum(types.NativeExecutionStatus.unsupported));
    try std.testing.expectEqual(@as(u2, 2), @intFromEnum(types.NativeExecutionStatus.@"error"));
}

test "enum values: WGPUBackendType" {
    try std.testing.expectEqual(@as(u32, 0), @intFromEnum(types.WGPUBackendType.undefined));
    try std.testing.expectEqual(@as(u32, 1), @intFromEnum(types.WGPUBackendType.nullBackend));
    try std.testing.expectEqual(@as(u32, 2), @intFromEnum(types.WGPUBackendType.webgpu));
    try std.testing.expectEqual(@as(u32, 3), @intFromEnum(types.WGPUBackendType.d3d11));
    try std.testing.expectEqual(@as(u32, 4), @intFromEnum(types.WGPUBackendType.d3d12));
    try std.testing.expectEqual(@as(u32, 5), @intFromEnum(types.WGPUBackendType.metal));
    try std.testing.expectEqual(@as(u32, 6), @intFromEnum(types.WGPUBackendType.vulkan));
    try std.testing.expectEqual(@as(u32, 7), @intFromEnum(types.WGPUBackendType.openGl));
    try std.testing.expectEqual(@as(u32, 8), @intFromEnum(types.WGPUBackendType.openGLES));
}

test "enum values: WGPURequestAdapterStatus" {
    try std.testing.expectEqual(@as(u32, 1), @intFromEnum(types.WGPURequestAdapterStatus.success));
    try std.testing.expectEqual(@as(u32, 2), @intFromEnum(types.WGPURequestAdapterStatus.callbackCancelled));
    try std.testing.expectEqual(@as(u32, 3), @intFromEnum(types.WGPURequestAdapterStatus.unavailable));
    try std.testing.expectEqual(@as(u32, 4), @intFromEnum(types.WGPURequestAdapterStatus.@"error"));
}

test "enum values: WGPURequestDeviceStatus" {
    try std.testing.expectEqual(@as(u32, 1), @intFromEnum(types.WGPURequestDeviceStatus.success));
    try std.testing.expectEqual(@as(u32, 2), @intFromEnum(types.WGPURequestDeviceStatus.callbackCancelled));
    try std.testing.expectEqual(@as(u32, 3), @intFromEnum(types.WGPURequestDeviceStatus.@"error"));
}

test "enum values: WGPUQueueWorkDoneStatus" {
    try std.testing.expectEqual(@as(u32, 1), @intFromEnum(types.WGPUQueueWorkDoneStatus.success));
    try std.testing.expectEqual(@as(u32, 2), @intFromEnum(types.WGPUQueueWorkDoneStatus.callbackCancelled));
    try std.testing.expectEqual(@as(u32, 3), @intFromEnum(types.WGPUQueueWorkDoneStatus.@"error"));
}

test "enum values: WGPUWaitStatus" {
    try std.testing.expectEqual(@as(u32, 1), @intFromEnum(types.WGPUWaitStatus.success));
    try std.testing.expectEqual(@as(u32, 2), @intFromEnum(types.WGPUWaitStatus.timedOut));
    try std.testing.expectEqual(@as(u32, 3), @intFromEnum(types.WGPUWaitStatus.@"error"));
}

test "enum values: WGPUDeviceLostReason" {
    try std.testing.expectEqual(@as(u32, 1), @intFromEnum(types.WGPUDeviceLostReason.unknown));
    try std.testing.expectEqual(@as(u32, 2), @intFromEnum(types.WGPUDeviceLostReason.destroyed));
    try std.testing.expectEqual(@as(u32, 3), @intFromEnum(types.WGPUDeviceLostReason.callbackCancelled));
    try std.testing.expectEqual(@as(u32, 4), @intFromEnum(types.WGPUDeviceLostReason.failedCreation));
}

test "enum values: WGPUErrorType" {
    try std.testing.expectEqual(@as(u32, 1), @intFromEnum(types.WGPUErrorType.noError));
    try std.testing.expectEqual(@as(u32, 2), @intFromEnum(types.WGPUErrorType.validation));
    try std.testing.expectEqual(@as(u32, 3), @intFromEnum(types.WGPUErrorType.outOfMemory));
    try std.testing.expectEqual(@as(u32, 4), @intFromEnum(types.WGPUErrorType.internal));
    try std.testing.expectEqual(@as(u32, 5), @intFromEnum(types.WGPUErrorType.unknown));
}

test "enum values: WGPUPowerPreference" {
    try std.testing.expectEqual(@as(u32, 0), @intFromEnum(types.WGPUPowerPreference.undefined));
    try std.testing.expectEqual(@as(u32, 1), @intFromEnum(types.WGPUPowerPreference.lowPower));
    try std.testing.expectEqual(@as(u32, 2), @intFromEnum(types.WGPUPowerPreference.highPerformance));
}

test "enum values: WGPUFeatureLevel" {
    try std.testing.expectEqual(@as(u32, 0), @intFromEnum(types.WGPUFeatureLevel.undefined));
    try std.testing.expectEqual(@as(u32, 1), @intFromEnum(types.WGPUFeatureLevel.compatibility));
    try std.testing.expectEqual(@as(u32, 2), @intFromEnum(types.WGPUFeatureLevel.core));
}

test "enum values: WGPUCallbackMode constants" {
    try std.testing.expectEqual(@as(u32, 0x00000001), types.WGPUCallbackMode_WaitAnyOnly);
    try std.testing.expectEqual(@as(u32, 0x00000002), types.WGPUCallbackMode_AllowProcessEvents);
    try std.testing.expectEqual(@as(u32, 0x00000003), types.WGPUCallbackMode_AllowSpontaneous);
}

test "enum values: CommandKind tag order is stable" {
    // CommandKind is enum(u8) — tag order is ABI for the JS-to-Zig command dispatch.
    try std.testing.expectEqual(@as(u8, 0), @intFromEnum(command.CommandKind.upload));
    try std.testing.expectEqual(@as(u8, 1), @intFromEnum(command.CommandKind.buffer_write));
    try std.testing.expectEqual(@as(u8, 2), @intFromEnum(command.CommandKind.copy_buffer_to_texture));
    try std.testing.expectEqual(@as(u8, 3), @intFromEnum(command.CommandKind.barrier));
    try std.testing.expectEqual(@as(u8, 4), @intFromEnum(command.CommandKind.dispatch));
    try std.testing.expectEqual(@as(u8, 5), @intFromEnum(command.CommandKind.dispatch_indirect));
    try std.testing.expectEqual(@as(u8, 6), @intFromEnum(command.CommandKind.kernel_dispatch));
    try std.testing.expectEqual(@as(u8, 7), @intFromEnum(command.CommandKind.render_draw));
    try std.testing.expectEqual(@as(u8, 8), @intFromEnum(command.CommandKind.draw_indirect));
    try std.testing.expectEqual(@as(u8, 9), @intFromEnum(command.CommandKind.draw_indexed_indirect));
    try std.testing.expectEqual(@as(u8, 10), @intFromEnum(command.CommandKind.render_pass));
    try std.testing.expectEqual(@as(u8, 11), @intFromEnum(command.CommandKind.sampler_create));
    try std.testing.expectEqual(@as(u8, 12), @intFromEnum(command.CommandKind.sampler_destroy));
    try std.testing.expectEqual(@as(u8, 13), @intFromEnum(command.CommandKind.texture_write));
    try std.testing.expectEqual(@as(u8, 14), @intFromEnum(command.CommandKind.texture_query));
    try std.testing.expectEqual(@as(u8, 15), @intFromEnum(command.CommandKind.texture_destroy));
    try std.testing.expectEqual(@as(u8, 16), @intFromEnum(command.CommandKind.surface_create));
    try std.testing.expectEqual(@as(u8, 17), @intFromEnum(command.CommandKind.surface_capabilities));
    try std.testing.expectEqual(@as(u8, 18), @intFromEnum(command.CommandKind.surface_configure));
    try std.testing.expectEqual(@as(u8, 19), @intFromEnum(command.CommandKind.surface_acquire));
    try std.testing.expectEqual(@as(u8, 20), @intFromEnum(command.CommandKind.surface_present));
    try std.testing.expectEqual(@as(u8, 21), @intFromEnum(command.CommandKind.surface_unconfigure));
    try std.testing.expectEqual(@as(u8, 22), @intFromEnum(command.CommandKind.surface_release));
    try std.testing.expectEqual(@as(u8, 23), @intFromEnum(command.CommandKind.async_diagnostics));
    try std.testing.expectEqual(@as(u8, 24), @intFromEnum(command.CommandKind.map_async));
}

test "enum values: CommandKind variant count is stable" {
    const fields = @typeInfo(command.CommandKind).@"enum".fields;
    try std.testing.expectEqual(@as(usize, 25), fields.len);
}

// ============================================================
// 5. Function/decl existence — verify key exports are accessible
// ============================================================

test "decl existence: ffi module re-exports NativeExecutionStatus" {
    // These are the types that cross the FFI boundary from webgpu_ffi.zig
    const S = ffi.NativeExecutionStatus;
    try std.testing.expect(@sizeOf(S) > 0);
}

test "decl existence: ffi module re-exports NativeExecutionResult" {
    const R = ffi.NativeExecutionResult;
    try std.testing.expect(@sizeOf(R) > 0);
}

test "decl existence: ffi UploadBufferUsageMode has expected variants" {
    const info = @typeInfo(ffi.UploadBufferUsageMode).@"enum";
    try std.testing.expectEqual(@as(usize, 2), info.fields.len);
    // Verify variants exist by name
    try std.testing.expect(std.meta.stringToEnum(ffi.UploadBufferUsageMode, "copy_dst_copy_src") != null);
    try std.testing.expect(std.meta.stringToEnum(ffi.UploadBufferUsageMode, "copy_dst") != null);
}

test "decl existence: ffi QueueWaitMode has expected variants" {
    const info = @typeInfo(ffi.QueueWaitMode).@"enum";
    try std.testing.expectEqual(@as(usize, 2), info.fields.len);
    try std.testing.expect(std.meta.stringToEnum(ffi.QueueWaitMode, "process_events") != null);
    try std.testing.expect(std.meta.stringToEnum(ffi.QueueWaitMode, "wait_any") != null);
}

test "decl existence: ffi QueueSyncMode has expected variants" {
    const info = @typeInfo(ffi.QueueSyncMode).@"enum";
    try std.testing.expectEqual(@as(usize, 2), info.fields.len);
    try std.testing.expect(std.meta.stringToEnum(ffi.QueueSyncMode, "per_command") != null);
    try std.testing.expect(std.meta.stringToEnum(ffi.QueueSyncMode, "deferred") != null);
}

test "decl existence: ffi GpuTimestampMode has expected variants" {
    const info = @typeInfo(ffi.GpuTimestampMode).@"enum";
    try std.testing.expectEqual(@as(usize, 3), info.fields.len);
    try std.testing.expect(std.meta.stringToEnum(ffi.GpuTimestampMode, "auto") != null);
    try std.testing.expect(std.meta.stringToEnum(ffi.GpuTimestampMode, "off") != null);
    try std.testing.expect(std.meta.stringToEnum(ffi.GpuTimestampMode, "require") != null);
}

test "decl existence: WebGPUBackend has core public methods" {
    const Backend = ffi.WebGPUBackend;
    // Verify key methods exist via @hasDecl
    try std.testing.expect(@hasDecl(Backend, "init"));
    try std.testing.expect(@hasDecl(Backend, "deinit"));
    try std.testing.expect(@hasDecl(Backend, "backendAvailable"));
    try std.testing.expect(@hasDecl(Backend, "executeCommand"));
    try std.testing.expect(@hasDecl(Backend, "setUploadBehavior"));
    try std.testing.expect(@hasDecl(Backend, "setQueueWaitMode"));
    try std.testing.expect(@hasDecl(Backend, "setQueueSyncMode"));
    try std.testing.expect(@hasDecl(Backend, "setGpuTimestampMode"));
    try std.testing.expect(@hasDecl(Backend, "gpuTimestampsEnabled"));
    try std.testing.expect(@hasDecl(Backend, "gpuTimestampsRequired"));
    try std.testing.expect(@hasDecl(Backend, "clearUncapturedError"));
    try std.testing.expect(@hasDecl(Backend, "takeUncapturedError"));
    try std.testing.expect(@hasDecl(Backend, "uncapturedErrorStatusMessage"));
    try std.testing.expect(@hasDecl(Backend, "effectiveLimits"));
    try std.testing.expect(@hasDecl(Backend, "prewarmUploadPath"));
    try std.testing.expect(@hasDecl(Backend, "prewarmKernelPipeline"));
    try std.testing.expect(@hasDecl(Backend, "runCapabilityIntrospection"));
    try std.testing.expect(@hasDecl(Backend, "getResourceTableProcs"));
    try std.testing.expect(@hasDecl(Backend, "getLifecycleProcs"));
    try std.testing.expect(@hasDecl(Backend, "releaseFullTextureViewsForTexture"));
}

test "decl existence: WebGPUBackend has queue sync methods" {
    const Backend = ffi.WebGPUBackend;
    try std.testing.expect(@hasDecl(Backend, "syncAfterSubmit"));
    try std.testing.expect(@hasDecl(Backend, "submitEmpty"));
    try std.testing.expect(@hasDecl(Backend, "submitCommandBuffers"));
    try std.testing.expect(@hasDecl(Backend, "submitInternal"));
    try std.testing.expect(@hasDecl(Backend, "flushQueue"));
    try std.testing.expect(@hasDecl(Backend, "waitForQueue"));
    try std.testing.expect(@hasDecl(Backend, "waitForQueueOnce"));
    try std.testing.expect(@hasDecl(Backend, "readTimestampBuffer"));
    try std.testing.expect(@hasDecl(Backend, "processEventsUntil"));
}

test "decl existence: WebGPUBackend has surface methods" {
    const Backend = ffi.WebGPUBackend;
    try std.testing.expect(@hasDecl(Backend, "createSurface"));
    try std.testing.expect(@hasDecl(Backend, "getSurfaceCapabilities"));
    try std.testing.expect(@hasDecl(Backend, "configureSurface"));
    try std.testing.expect(@hasDecl(Backend, "getCurrentSurfaceTexture"));
    try std.testing.expect(@hasDecl(Backend, "presentSurface"));
    try std.testing.expect(@hasDecl(Backend, "unconfigureSurface"));
    try std.testing.expect(@hasDecl(Backend, "releaseSurface"));
}

// ============================================================
// 6. Constant values — sentinel/flag values that cross FFI
// ============================================================

test "constant values: WGPU boolean constants" {
    try std.testing.expectEqual(@as(types.WGPUBool, 0), types.WGPU_FALSE);
    try std.testing.expectEqual(@as(types.WGPUBool, 1), types.WGPU_TRUE);
}

test "constant values: WGPU sentinel constants" {
    try std.testing.expectEqual(std.math.maxInt(usize), types.WGPU_STRLEN);
    try std.testing.expectEqual(@as(u32, 0xFFFFFFFF), types.WGPU_MIP_LEVEL_COUNT_UNDEFINED);
    try std.testing.expectEqual(@as(u32, 0xFFFFFFFF), types.WGPU_ARRAY_LAYER_COUNT_UNDEFINED);
}

test "constant values: WGPUBufferUsage flags" {
    try std.testing.expectEqual(@as(u64, 0x00), types.WGPUBufferUsage_None);
    try std.testing.expectEqual(@as(u64, 0x01), types.WGPUBufferUsage_MapRead);
    try std.testing.expectEqual(@as(u64, 0x02), types.WGPUBufferUsage_MapWrite);
    try std.testing.expectEqual(@as(u64, 0x04), types.WGPUBufferUsage_CopySrc);
    try std.testing.expectEqual(@as(u64, 0x08), types.WGPUBufferUsage_CopyDst);
    try std.testing.expectEqual(@as(u64, 0x10), types.WGPUBufferUsage_Index);
    try std.testing.expectEqual(@as(u64, 0x20), types.WGPUBufferUsage_Vertex);
    try std.testing.expectEqual(@as(u64, 0x40), types.WGPUBufferUsage_Uniform);
    try std.testing.expectEqual(@as(u64, 0x80), types.WGPUBufferUsage_Storage);
    try std.testing.expectEqual(@as(u64, 0x200), types.WGPUBufferUsage_QueryResolve);
}

test "constant values: WGPUTextureUsage flags" {
    try std.testing.expectEqual(@as(u64, 0x00), types.WGPUTextureUsage_None);
    try std.testing.expectEqual(@as(u64, 0x01), types.WGPUTextureUsage_CopySrc);
    try std.testing.expectEqual(@as(u64, 0x02), types.WGPUTextureUsage_CopyDst);
    try std.testing.expectEqual(@as(u64, 0x04), types.WGPUTextureUsage_TextureBinding);
    try std.testing.expectEqual(@as(u64, 0x08), types.WGPUTextureUsage_StorageBinding);
    try std.testing.expectEqual(@as(u64, 0x10), types.WGPUTextureUsage_RenderAttachment);
    try std.testing.expectEqual(@as(u64, 0x20), types.WGPUTextureUsage_TransientAttachment);
    try std.testing.expectEqual(@as(u64, 0x40), types.WGPUTextureUsage_StorageAttachment);
}

test "constant values: WGPUShaderStage flags" {
    try std.testing.expectEqual(@as(u64, 0x00), types.WGPUShaderStage_None);
    try std.testing.expectEqual(@as(u64, 0x01), types.WGPUShaderStage_Vertex);
    try std.testing.expectEqual(@as(u64, 0x02), types.WGPUShaderStage_Fragment);
    try std.testing.expectEqual(@as(u64, 0x04), types.WGPUShaderStage_Compute);
}

test "constant values: WGPUSType shader source constants" {
    try std.testing.expectEqual(@as(u32, 0x00000002), types.WGPUSType_ShaderSourceWGSL);
    try std.testing.expectEqual(@as(u32, 0x00000003), types.WGPUSType_ShaderSourceMSL);
    // Pinned vendor/webgpu-headers/webgpu.h; SPIR-V uses upstream sType 1.
    try std.testing.expectEqual(@as(u32, 0x00000001), types.WGPUSType_ShaderSourceSPIRV);
    try std.testing.expectEqual(@as(u32, 0x00000005), types.WGPUSType_ShaderSourceHLSL);
    try std.testing.expectEqual(@as(u32, 0x00050002), types.WGPUSType_ExternalTextureBindingLayout);
    try std.testing.expectEqual(@as(u32, 0x00050001), types.WGPUSType_ExternalTextureBindingEntry);
}

test "constant values: WGPUFeatureName key features" {
    try std.testing.expectEqual(@as(u32, 0x00000009), types.WGPUFeatureName_TimestampQuery);
    try std.testing.expectEqual(@as(u32, 0x00050003), types.WGPUFeatureName_ChromiumExperimentalTimestampQueryInsidePasses);
    try std.testing.expectEqual(@as(u32, 0x00050031), types.WGPUFeatureName_MultiDrawIndirect);
    try std.testing.expectEqual(@as(u32, 0x0005000A), types.WGPUFeatureName_PixelLocalStorageCoherent);
    try std.testing.expectEqual(@as(u32, 0x0005000B), types.WGPUFeatureName_PixelLocalStorageNonCoherent);
    try std.testing.expectEqual(@as(u32, 0x0005003A), types.WGPUFeatureName_ChromiumExperimentalSamplingResourceTable);
}

test "constant values: WGPUMapMode and WGPUStatus" {
    try std.testing.expectEqual(@as(u64, 0x01), types.WGPUMapMode_Read);
    try std.testing.expectEqual(@as(u64, 0x02), types.WGPUMapMode_Write);
    try std.testing.expectEqual(@as(u32, 1), types.WGPUMapAsyncStatus_Success);
    try std.testing.expectEqual(@as(u32, 1), types.WGPUStatus_Success);
}

test "constant values: TIMESTAMP_BUFFER_SIZE" {
    try std.testing.expectEqual(@as(u64, 16), types.TIMESTAMP_BUFFER_SIZE);
}

test "constant values: WGPUQueryType_Timestamp" {
    try std.testing.expectEqual(@as(u32, 0x00000002), types.WGPUQueryType_Timestamp);
}

// ============================================================
// 7. Handle type assertions — opaque pointer types are nullable
// ============================================================

test "handle types: all handle types are optional opaque pointers" {
    // Every WebGPU handle is ?*anyopaque — verify size and nullability
    const handle_size = @sizeOf(?*anyopaque);
    try std.testing.expectEqual(handle_size, @sizeOf(types.WGPUInstance));
    try std.testing.expectEqual(handle_size, @sizeOf(types.WGPUAdapter));
    try std.testing.expectEqual(handle_size, @sizeOf(types.WGPUDevice));
    try std.testing.expectEqual(handle_size, @sizeOf(types.WGPUQueue));
    try std.testing.expectEqual(handle_size, @sizeOf(types.WGPUBuffer));
    try std.testing.expectEqual(handle_size, @sizeOf(types.WGPUTexture));
    try std.testing.expectEqual(handle_size, @sizeOf(types.WGPUTextureView));
    try std.testing.expectEqual(handle_size, @sizeOf(types.WGPUExternalTexture));
    try std.testing.expectEqual(handle_size, @sizeOf(types.WGPUShaderModule));
    try std.testing.expectEqual(handle_size, @sizeOf(types.WGPUSampler));
    try std.testing.expectEqual(handle_size, @sizeOf(types.WGPUComputePipeline));
    try std.testing.expectEqual(handle_size, @sizeOf(types.WGPURenderPipeline));
    try std.testing.expectEqual(handle_size, @sizeOf(types.WGPUComputePassEncoder));
    try std.testing.expectEqual(handle_size, @sizeOf(types.WGPURenderPassEncoder));
    try std.testing.expectEqual(handle_size, @sizeOf(types.WGPUBindGroupLayout));
    try std.testing.expectEqual(handle_size, @sizeOf(types.WGPUBindGroup));
    try std.testing.expectEqual(handle_size, @sizeOf(types.WGPUPipelineLayout));
    try std.testing.expectEqual(handle_size, @sizeOf(types.WGPUCommandEncoder));
    try std.testing.expectEqual(handle_size, @sizeOf(types.WGPUCommandBuffer));
    try std.testing.expectEqual(handle_size, @sizeOf(types.WGPUQuerySet));
}

test "handle types: null is the default zero value" {
    // Handles default to null (important for zero-init of backend state)
    const inst: types.WGPUInstance = null;
    try std.testing.expect(inst == null);
    const dev: types.WGPUDevice = null;
    try std.testing.expect(dev == null);
    const buf: types.WGPUBuffer = null;
    try std.testing.expect(buf == null);
}

// ============================================================
// 8. Callback info struct layout — used in adapter/device request
// ============================================================

test "sizeof: WGPURequestAdapterCallbackInfo is 5 fields" {
    // nextInChain (ptr) + mode (u32) + padding + callback (ptr) + userdata1 (ptr) + userdata2 (ptr)
    try std.testing.expectEqual(@as(usize, 40), @sizeOf(types.WGPURequestAdapterCallbackInfo));
}

test "sizeof: WGPURequestDeviceCallbackInfo matches adapter variant" {
    try std.testing.expectEqual(
        @sizeOf(types.WGPURequestAdapterCallbackInfo),
        @sizeOf(types.WGPURequestDeviceCallbackInfo),
    );
}

test "sizeof: WGPUQueueWorkDoneCallbackInfo matches adapter variant" {
    try std.testing.expectEqual(
        @sizeOf(types.WGPURequestAdapterCallbackInfo),
        @sizeOf(types.WGPUQueueWorkDoneCallbackInfo),
    );
}

test "sizeof: WGPUDeviceLostCallbackInfo matches adapter variant" {
    try std.testing.expectEqual(
        @sizeOf(types.WGPURequestAdapterCallbackInfo),
        @sizeOf(types.WGPUDeviceLostCallbackInfo),
    );
}

test "sizeof: WGPUUncapturedErrorCallbackInfo is 4 fields (no mode)" {
    // nextInChain (ptr) + callback (ptr) + userdata1 (ptr) + userdata2 (ptr)
    try std.testing.expectEqual(@as(usize, 32), @sizeOf(types.WGPUUncapturedErrorCallbackInfo));
}

test "field offsets: WGPURequestAdapterCallbackInfo" {
    try std.testing.expectEqual(@as(usize, 0), @offsetOf(types.WGPURequestAdapterCallbackInfo, "nextInChain"));
    try std.testing.expectEqual(@as(usize, 8), @offsetOf(types.WGPURequestAdapterCallbackInfo, "mode"));
    try std.testing.expectEqual(@as(usize, 16), @offsetOf(types.WGPURequestAdapterCallbackInfo, "callback"));
    try std.testing.expectEqual(@as(usize, 24), @offsetOf(types.WGPURequestAdapterCallbackInfo, "userdata1"));
    try std.testing.expectEqual(@as(usize, 32), @offsetOf(types.WGPURequestAdapterCallbackInfo, "userdata2"));
}

test "field offsets: WGPUUncapturedErrorCallbackInfo" {
    try std.testing.expectEqual(@as(usize, 0), @offsetOf(types.WGPUUncapturedErrorCallbackInfo, "nextInChain"));
    try std.testing.expectEqual(@as(usize, 8), @offsetOf(types.WGPUUncapturedErrorCallbackInfo, "callback"));
    try std.testing.expectEqual(@as(usize, 16), @offsetOf(types.WGPUUncapturedErrorCallbackInfo, "userdata1"));
    try std.testing.expectEqual(@as(usize, 24), @offsetOf(types.WGPUUncapturedErrorCallbackInfo, "userdata2"));
}

// ============================================================
// 9. WGPUFlags type assertions — u64 on all platforms
// ============================================================

test "typedef: WGPUFlags is u64" {
    try std.testing.expectEqual(@as(usize, 8), @sizeOf(types.WGPUFlags));
}

test "typedef: WGPUBufferUsage is WGPUFlags" {
    try std.testing.expectEqual(@sizeOf(types.WGPUFlags), @sizeOf(types.WGPUBufferUsage));
}

test "typedef: WGPUTextureUsage is WGPUFlags" {
    try std.testing.expectEqual(@sizeOf(types.WGPUFlags), @sizeOf(types.WGPUTextureUsage));
}

test "typedef: WGPUShaderStageFlags is WGPUFlags" {
    try std.testing.expectEqual(@sizeOf(types.WGPUFlags), @sizeOf(types.WGPUShaderStageFlags));
}

test "typedef: WGPUBool is u32" {
    try std.testing.expectEqual(@as(usize, 4), @sizeOf(types.WGPUBool));
}

// ============================================================
// 10. initLimits produces a zeroed struct with null chain
// ============================================================

test "initLimits: produces zeroed struct with null nextInChain" {
    const limits = types.initLimits();
    try std.testing.expect(limits.nextInChain == null);
    try std.testing.expectEqual(@as(u32, 0), limits.maxTextureDimension1D);
    try std.testing.expectEqual(@as(u32, 0), limits.maxTextureDimension2D);
    try std.testing.expectEqual(@as(u32, 0), limits.maxComputeWorkgroupSizeX);
    try std.testing.expectEqual(@as(u64, 0), limits.maxBufferSize);
    try std.testing.expectEqual(@as(u64, 0), limits.maxStorageBufferBindingSize);
}

// ============================================================
// 11. uncapturedErrorStatusMessage returns expected strings
// ============================================================

test "uncapturedErrorStatusMessage: known error types map to expected messages" {
    const Backend = ffi.WebGPUBackend;
    const validation_msg = Backend.uncapturedErrorStatusMessage(.validation);
    try std.testing.expect(std.mem.indexOf(u8, validation_msg, "validation") != null);

    const oom_msg = Backend.uncapturedErrorStatusMessage(.outOfMemory);
    try std.testing.expect(std.mem.indexOf(u8, oom_msg, "out-of-memory") != null);

    const internal_msg = Backend.uncapturedErrorStatusMessage(.internal);
    try std.testing.expect(std.mem.indexOf(u8, internal_msg, "internal") != null);

    const unknown_msg = Backend.uncapturedErrorStatusMessage(.unknown);
    try std.testing.expect(std.mem.indexOf(u8, unknown_msg, "unknown") != null);
}

// ============================================================
// 12. Texture binding/storage enum constants
// ============================================================

test "constant values: WGPUTextureSampleType" {
    try std.testing.expectEqual(@as(u32, 0x00000000), types.WGPUTextureSampleType_BindingNotUsed);
    try std.testing.expectEqual(@as(u32, 0x00000001), types.WGPUTextureSampleType_Undefined);
    try std.testing.expectEqual(@as(u32, 0x00000002), types.WGPUTextureSampleType_Float);
    try std.testing.expectEqual(@as(u32, 0x00000003), types.WGPUTextureSampleType_UnfilterableFloat);
    try std.testing.expectEqual(@as(u32, 0x00000004), types.WGPUTextureSampleType_Depth);
    try std.testing.expectEqual(@as(u32, 0x00000005), types.WGPUTextureSampleType_Sint);
    try std.testing.expectEqual(@as(u32, 0x00000006), types.WGPUTextureSampleType_Uint);
}

test "constant values: WGPUStorageTextureAccess" {
    try std.testing.expectEqual(@as(u32, 0x00000000), types.WGPUStorageTextureAccess_BindingNotUsed);
    try std.testing.expectEqual(@as(u32, 0x00000001), types.WGPUStorageTextureAccess_Undefined);
    try std.testing.expectEqual(@as(u32, 0x00000002), types.WGPUStorageTextureAccess_WriteOnly);
    try std.testing.expectEqual(@as(u32, 0x00000003), types.WGPUStorageTextureAccess_ReadOnly);
    try std.testing.expectEqual(@as(u32, 0x00000004), types.WGPUStorageTextureAccess_ReadWrite);
}

test "constant values: WGPUBufferBindingType" {
    try std.testing.expectEqual(@as(u32, 0x00000000), types.WGPUBufferBindingType_BindingNotUsed);
    try std.testing.expectEqual(@as(u32, 0x00000001), types.WGPUBufferBindingType_Undefined);
    try std.testing.expectEqual(@as(u32, 0x00000002), types.WGPUBufferBindingType_Uniform);
    try std.testing.expectEqual(@as(u32, 0x00000003), types.WGPUBufferBindingType_Storage);
    try std.testing.expectEqual(@as(u32, 0x00000004), types.WGPUBufferBindingType_ReadOnlyStorage);
}

test "constant values: WGPUTextureViewDimension" {
    try std.testing.expectEqual(@as(u32, 0x00000000), types.WGPUTextureViewDimension_Undefined);
    try std.testing.expectEqual(@as(u32, 0x00000001), types.WGPUTextureViewDimension_1D);
    try std.testing.expectEqual(@as(u32, 0x00000002), types.WGPUTextureViewDimension_2D);
    try std.testing.expectEqual(@as(u32, 0x00000003), types.WGPUTextureViewDimension_2DArray);
    try std.testing.expectEqual(@as(u32, 0x00000004), types.WGPUTextureViewDimension_Cube);
    try std.testing.expectEqual(@as(u32, 0x00000005), types.WGPUTextureViewDimension_CubeArray);
    try std.testing.expectEqual(@as(u32, 0x00000006), types.WGPUTextureViewDimension_3D);
}

test "constant values: WGPUTextureDimension" {
    try std.testing.expectEqual(@as(u32, 0), types.WGPUTextureDimension_Undefined);
    try std.testing.expectEqual(@as(u32, 1), types.WGPUTextureDimension_1D);
    try std.testing.expectEqual(@as(u32, 2), types.WGPUTextureDimension_2D);
    try std.testing.expectEqual(@as(u32, 3), types.WGPUTextureDimension_3D);
}

test "constant values: WGPUSamplerBindingType" {
    try std.testing.expectEqual(@as(u32, 0x00000000), types.WGPUSamplerBindingType_BindingNotUsed);
    try std.testing.expectEqual(@as(u32, 0x00000001), types.WGPUSamplerBindingType_Undefined);
    try std.testing.expectEqual(@as(u32, 0x00000002), types.WGPUSamplerBindingType_Filtering);
    try std.testing.expectEqual(@as(u32, 0x00000003), types.WGPUSamplerBindingType_NonFiltering);
    try std.testing.expectEqual(@as(u32, 0x00000004), types.WGPUSamplerBindingType_Comparison);
}

// ============================================================
// 13. Texture format constants — first/last format range
// ============================================================

test "constant values: core texture format range" {
    try std.testing.expectEqual(@as(u32, 0x00000000), types.WGPUTextureFormat_Undefined);
    try std.testing.expectEqual(@as(u32, 0x00000001), types.WGPUTextureFormat_R8Unorm);
    try std.testing.expectEqual(@as(u32, 0x00000016), types.WGPUTextureFormat_RGBA8Unorm);
    try std.testing.expectEqual(@as(u32, 0x0000001B), types.WGPUTextureFormat_BGRA8Unorm);
    try std.testing.expectEqual(@as(u32, 0x00000030), types.WGPUTextureFormat_Depth32Float);
    try std.testing.expectEqual(@as(u32, 0x00000031), types.WGPUTextureFormat_Depth32FloatStencil8);
}

const CaptureProbe = struct {
    const records = @import("../../src/backend/webgpu_backend_types.zig");
    const proc_aliases = @import("../../src/core/abi/wgpu_type_proc_aliases.zig");
    var queue_callback: types.WGPUQueueWorkDoneCallbackInfo = undefined;
    var map_callback: types.WGPUBufferMapCallbackInfo = undefined;
    var queue_ready = true;
    var map_ready = true;
    var map_success = true;
    var queue_success = true;
    var released_buffers: u32 = 0;
    var released_commands: u32 = 0;
    var released_devices: u32 = 0;
    var map_calls: u32 = 0;
    var submits: u32 = 0;
    var bytes = [_]u8{ 11, 22, 33, 44 };

    fn reset() void {
        queue_ready = true;
        map_ready = true;
        map_success = true;
        queue_success = true;
        released_buffers = 0;
        released_commands = 0;
        released_devices = 0;
        map_calls = 0;
        submits = 0;
    }
    fn handle(comptime T: type, address: usize) T {
        return @ptrFromInt(address);
    }
    fn createBuffer(_: types.WGPUDevice, _: ?*const types.WGPUBufferDescriptor) callconv(.c) types.WGPUBuffer {
        return handle(types.WGPUBuffer, 0x2000);
    }
    fn createEncoder(_: types.WGPUDevice, _: ?*const types.WGPUCommandEncoderDescriptor) callconv(.c) types.WGPUCommandEncoder {
        return handle(types.WGPUCommandEncoder, 0x3000);
    }
    fn copy(_: types.WGPUCommandEncoder, _: types.WGPUBuffer, _: u64, _: types.WGPUBuffer, _: u64, _: u64) callconv(.c) void {}
    fn finish(_: types.WGPUCommandEncoder, _: ?*const types.WGPUCommandBufferDescriptor) callconv(.c) types.WGPUCommandBuffer {
        return handle(types.WGPUCommandBuffer, 0x4000);
    }
    fn releaseEncoder(_: types.WGPUCommandEncoder) callconv(.c) void {}
    fn releaseCommands(_: types.WGPUCommandBuffer) callconv(.c) void {
        released_commands += 1;
    }
    fn releaseBuffer(_: types.WGPUBuffer) callconv(.c) void {
        released_buffers += 1;
    }
    fn releaseDevice(_: types.WGPUDevice) callconv(.c) void {
        released_devices += 1;
    }
    fn releaseQueue(_: types.WGPUQueue) callconv(.c) void {}
    fn releaseAdapter(_: types.WGPUAdapter) callconv(.c) void {}
    fn releaseInstance(_: types.WGPUInstance) callconv(.c) void {}
    fn unmap(_: types.WGPUBuffer) callconv(.c) void {}
    fn submit(_: types.WGPUQueue, _: usize, _: [*c]types.WGPUCommandBuffer) callconv(.c) void {
        submits += 1;
    }
    fn queueDone(_: types.WGPUQueue, callback: types.WGPUQueueWorkDoneCallbackInfo) callconv(.c) types.WGPUFuture {
        queue_callback = callback;
        return .{ .id = 1 };
    }
    fn map(_: types.WGPUBuffer, _: types.WGPUMapMode, _: usize, _: usize, callback: types.WGPUBufferMapCallbackInfo) callconv(.c) types.WGPUFuture {
        map_calls += 1;
        map_callback = callback;
        return .{ .id = 2 };
    }
    fn mapped(_: types.WGPUBuffer, _: usize, _: usize) callconv(.c) ?*const anyopaque {
        return &bytes;
    }
    fn wait(_: types.WGPUInstance, _: usize, infos: [*]types.WGPUFutureWaitInfo, timeout: u64) callconv(.c) types.WGPUWaitStatus {
        std.debug.assert(timeout == 0);
        if (infos[0].future.id == 1) {
            if (!queue_ready) return .timedOut;
            queue_callback.callback.?(if (queue_success) .success else .@"error", .{ .data = null, .length = 0 }, queue_callback.userdata1, null);
        } else {
            if (!map_ready) return .timedOut;
            map_callback.callback.?(if (map_success) types.WGPUMapAsyncStatus_Success else 2, .{ .data = null, .length = 0 }, map_callback.userdata1, null);
        }
        infos[0].completed = types.WGPU_TRUE;
        return .success;
    }
    fn backend() !ffi.WebGPUBackend {
        reset();
        const a = std.testing.allocator;
        var procs: proc_aliases.Procs = undefined;
        procs.wgpuDeviceCreateBuffer = createBuffer;
        procs.wgpuDeviceCreateCommandEncoder = createEncoder;
        procs.wgpuCommandEncoderCopyBufferToBuffer = copy;
        procs.wgpuCommandEncoderFinish = finish;
        procs.wgpuCommandEncoderRelease = releaseEncoder;
        procs.wgpuCommandBufferRelease = releaseCommands;
        procs.wgpuBufferRelease = releaseBuffer;
        procs.wgpuBufferUnmap = unmap;
        procs.wgpuQueueSubmit = submit;
        procs.wgpuQueueOnSubmittedWorkDone = queueDone;
        procs.wgpuBufferMapAsync = map;
        procs.wgpuBufferGetConstMappedRange = mapped;
        procs.wgpuInstanceWaitAny = wait;
        procs.wgpuDeviceRelease = releaseDevice;
        procs.wgpuQueueRelease = releaseQueue;
        procs.wgpuAdapterRelease = releaseAdapter;
        procs.wgpuInstanceRelease = releaseInstance;
        var result = ffi.WebGPUBackend{
            .core = .{
                .allocator = a,
                .webgpu_ffi_queue_wait_timeout_ns = 0,
                .procs = procs,
                .device = handle(types.WGPUDevice, 0x5000),
                .queue = handle(types.WGPUQueue, 0x6000),
                .instance = handle(types.WGPUInstance, 0x7000),
                .buffers = std.AutoHashMap(u64, records.BufferRecord).init(a),
                .textures = std.AutoHashMap(u64, records.TextureRecord).init(a),
                .pipeline_cache = std.AutoHashMap(u64, records.PipelineCacheEntry).init(a),
            },
            .full = .{
                .render_pipeline_cache = std.AutoHashMap(u32, records.RenderPipelineCacheEntry).init(a),
                .render_target_view_cache = std.AutoHashMap(u64, records.RenderTextureViewCacheEntry).init(a),
                .render_depth_view_cache = std.AutoHashMap(u64, records.RenderTextureViewCacheEntry).init(a),
                .samplers = std.AutoHashMap(u64, types.WGPUSampler).init(a),
                .surfaces = std.AutoHashMap(u64, ffi.ManagedSurface).init(a),
            },
        };
        try result.core.buffers.put(1, .{
            .buffer = handle(types.WGPUBuffer, 0x1000),
            .size = 4,
            .usage = types.WGPUBufferUsage_CopySrc,
        });
        return result;
    }
};

test "Dawn capture lease: successful bytes and exact terminal release" {
    var backend = try CaptureProbe.backend();
    const output = try backend.captureBuffer(std.testing.allocator, 1, 0, 4);
    defer std.testing.allocator.free(output);
    try std.testing.expectEqualSlices(u8, &.{ 11, 22, 33, 44 }, output);
    try std.testing.expect(backend.core.pending_capture == null);
    try std.testing.expectEqual(@as(u32, 1), CaptureProbe.released_buffers);
    try std.testing.expectEqual(@as(u32, 1), CaptureProbe.released_commands);
    try std.testing.expect(backend.deinit());
    try std.testing.expectEqual(@as(u32, 2), CaptureProbe.released_buffers);
}

test "Dawn capture lease: queue timeout retains resources and blocks new work" {
    var backend = try CaptureProbe.backend();
    CaptureProbe.queue_ready = false;
    try std.testing.expectError(error.WaitTimedOut, backend.captureBuffer(std.testing.allocator, 1, 0, 4));
    try std.testing.expectEqual(@as(u32, 0), CaptureProbe.map_calls);
    try std.testing.expectEqual(@as(u32, 0), CaptureProbe.released_buffers);
    try std.testing.expectEqual(@as(u32, 0), CaptureProbe.released_commands);
    try std.testing.expectError(error.WaitTimedOut, backend.captureBuffer(std.testing.allocator, 1, 0, 4));
    try std.testing.expectEqual(@as(u32, 1), CaptureProbe.submits);
    try std.testing.expectError(error.WaitTimedOut, backend.flushQueue());
    try std.testing.expectError(error.WaitTimedOut, backend.executeBufferWriteBytes(1, 0, 4, &.{ 1, 2, 3, 4 }));
    try std.testing.expectEqual(@as(u32, 1), CaptureProbe.submits);
    try std.testing.expect(!backend.deinit());
    try std.testing.expectEqual(@as(u32, 0), CaptureProbe.released_devices);
    CaptureProbe.queue_ready = true;
    try std.testing.expect(backend.deinit());
    try std.testing.expect(backend.core.pending_capture == null);
    try std.testing.expectEqual(@as(u32, 2), CaptureProbe.released_buffers);
    try std.testing.expectEqual(@as(u32, 1), CaptureProbe.released_commands);
    try std.testing.expect(backend.deinit());
    try std.testing.expectEqual(@as(u32, 1), CaptureProbe.released_devices);
}

test "Dawn capture lease: late map callback survives failed early teardown" {
    var backend = try CaptureProbe.backend();
    CaptureProbe.map_ready = false;
    try std.testing.expectError(error.WaitTimedOut, backend.captureBuffer(std.testing.allocator, 1, 0, 4));
    try std.testing.expect(!backend.deinit());
    try std.testing.expectEqual(@as(u32, 0), CaptureProbe.released_buffers);
    // Deliver through the saved heap userdata after captureBuffer returned.
    CaptureProbe.map_ready = true;
    try std.testing.expect(backend.deinit());
    try std.testing.expectEqual(@as(u32, 2), CaptureProbe.released_buffers);
    try std.testing.expectEqual(@as(u32, 1), CaptureProbe.released_commands);
    try std.testing.expectEqual(@as(u32, 1), CaptureProbe.released_devices);
    try std.testing.expectEqual(error.WaitTimedOut, backend.core.capture_error.?);
}

test "Dawn capture lease: terminal failed map retires without returning output" {
    var backend = try CaptureProbe.backend();
    CaptureProbe.map_success = false;
    try std.testing.expectError(error.BufferMapFailed, backend.captureBuffer(std.testing.allocator, 1, 0, 4));
    try std.testing.expect(backend.core.pending_capture == null);
    try std.testing.expectEqual(@as(u32, 1), CaptureProbe.released_buffers);
    try std.testing.expect(backend.deinit());
}

test "Dawn capture lease: delegate allocation survives unknown teardown" {
    const dawn = @import("../../src/backend/dawn_delegate_backend.zig");
    const owner = try std.testing.allocator.create(dawn.DawnDelegateBackend);
    owner.* = .{ .allocator = std.testing.allocator, .inner = try CaptureProbe.backend(), .effective_id = .dawn_delegate };
    CaptureProbe.map_ready = false;
    try std.testing.expectError(error.WaitTimedOut, owner.inner.captureBuffer(std.testing.allocator, 1, 0, 4));
    try std.testing.expect(!dawn.destroyContext(owner));
    try std.testing.expect(owner.inner.core.pending_capture != null);
    try std.testing.expectEqual(@as(u32, 0), CaptureProbe.released_devices);
    CaptureProbe.map_ready = true;
    try std.testing.expect(dawn.destroyContext(owner));
    try std.testing.expectEqual(@as(u32, 2), CaptureProbe.released_buffers);
    try std.testing.expectEqual(@as(u32, 1), CaptureProbe.released_devices);
}
