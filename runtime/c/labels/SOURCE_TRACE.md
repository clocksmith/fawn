# Actual label-store source trace

Captured at HEAD da9b75a09e93248f52749d5aa7dd414f4d8a0016; exact working-tree hashes in SOURCE_INPUTS.json.
This is static source tracing, not execution or GPU evidence.

## Direct owner imports

- runtime/zig/src/native/api/doe_native_api_misc_exports.zig:32: pub const label_store = @import("../support/doe_label_store.zig");
- runtime/zig/src/native/support/doe_native_object_helpers.zig:11: pub const label_store = @import("doe_label_store.zig");

Both must route through one facade. native/mod.zig re-exports the misc owner.
No direct label_store.get caller exists outside the retained export wrapper:
doeNativeObjectGetLabel calls get(handle), returns pointer after mutex unlock.

## Every direct map mutation/lookup callsite

- runtime/zig/src/dropin/dropin_browser_shared_memory.zig:156 | fn labelOwnedObject | native.label_store.set(raw, label.data, label.length);
- runtime/zig/src/dropin/dropin_browser_shared_memory.zig:533 | pub fn wgpuSharedFenceRelease | native.label_store.remove(shared_fence);
- runtime/zig/src/dropin/dropin_browser_shared_memory.zig:741 | pub fn wgpuSharedTextureMemoryRelease | native.label_store.remove(shared_texture_memory);
- runtime/zig/src/native/command/doe_encoder_native.zig:45 | pub export fn doeNativeDeviceCreateCommandEncoder | if (desc) |d| label_store.set(result, d.label.data, d.label.length);
- runtime/zig/src/native/command/doe_encoder_native.zig:70 | pub export fn doeNativeCommandEncoderRelease | label_store.remove(raw);
- runtime/zig/src/native/command/doe_encoder_native.zig:209 | pub export fn doeNativeCommandEncoderFinish | if (desc) |d| label_store.set(result, d.label.data, d.label.length);
- runtime/zig/src/native/command/doe_encoder_native.zig:216 | pub export fn doeNativeCommandBufferRelease | label_store.remove(raw);
- runtime/zig/src/native/compute/doe_compute_ext_native.zig:323 | pub export fn doeNativeComputePassRelease | native_helpers.label_store.remove(raw);
- runtime/zig/src/native/lifecycle/doe_instance_device_native.zig:525 | fn instanceRemoveLabel | native_helpers.label_store.remove(toOpaque(instance));
- runtime/zig/src/native/lifecycle/doe_instance_device_native.zig:576 | pub export fn doeNativeInstanceRelease | native_helpers.label_store.remove(raw);
- runtime/zig/src/native/lifecycle/doe_instance_device_native.zig:678 | pub export fn doeNativeAdapterRelease | label_store.remove(raw);
- runtime/zig/src/native/lifecycle/doe_instance_device_native.zig:815 | pub export fn doeNativeDeviceReleaseInternal | label_store.remove(raw);
- runtime/zig/src/native/queue/doe_queue_lifecycle.zig:238 | pub fn doeNativeQueueRelease | native_helpers.label_store.remove(raw);
- runtime/zig/src/native/render/doe_bundle_native.zig:70 | pub export fn doeNativeRenderBundleEncoderRelease | native_helpers.label_store.remove(raw);
- runtime/zig/src/native/render/doe_bundle_native.zig:293 | pub export fn doeNativeRenderBundleRelease | native_helpers.label_store.remove(raw);
- runtime/zig/src/native/render/doe_render_native.zig:515 | pub export fn doeNativeRenderPassRelease | label_store.remove(raw);
- runtime/zig/src/native/render/doe_render_pipeline_native.zig:571 | pub export fn doeNativeRenderPipelineRelease | native_helpers.label_store.remove(raw);
- runtime/zig/src/native/resource/doe_bind_group_native.zig:256 | pub export fn doeNativeDeviceCreateBindGroupLayout | label_store.set(result, d.label.data, d.label.length);
- runtime/zig/src/native/resource/doe_bind_group_native.zig:286 | pub export fn doeNativeBindGroupLayoutRelease | label_store.remove(raw);
- runtime/zig/src/native/resource/doe_bind_group_native.zig:456 | pub export fn doeNativeDeviceCreateBindGroup | label_store.set(bg_result, d.label.data, d.label.length);
- runtime/zig/src/native/resource/doe_bind_group_native.zig:516 | pub export fn doeNativeBindGroupRelease | label_store.remove(raw);
- runtime/zig/src/native/resource/doe_bind_group_native.zig:568 | pub export fn doeNativeDeviceCreatePipelineLayout | label_store.set(pl_result, pd.label.data, pd.label.length);
- runtime/zig/src/native/resource/doe_bind_group_native.zig:591 | pub export fn doeNativePipelineLayoutRelease | label_store.remove(raw);
- runtime/zig/src/native/resource/doe_buffer_ops_native.zig:164 | fn createBuffer | label_store.set(result, d.label.data, d.label.length);
- runtime/zig/src/native/resource/doe_buffer_ops_native.zig:196 | fn createBuffer | label_store.set(result, d.label.data, d.label.length);
- runtime/zig/src/native/resource/doe_buffer_ops_native.zig:217 | fn createBuffer | label_store.set(result, d.label.data, d.label.length);
- runtime/zig/src/native/resource/doe_buffer_ops_native.zig:235 | pub export fn doeNativeBufferRelease | label_store.remove(raw);
- runtime/zig/src/native/resource/doe_query_native.zig:301 | pub export fn doeNativeQuerySetRelease | native_helpers.label_store.remove(qs_raw);
- runtime/zig/src/native/resource/doe_texture_sampler_native.zig:435 | fn createTexture | label_store.set(result, d.label.data, d.label.length);
- runtime/zig/src/native/resource/doe_texture_sampler_native.zig:481 | fn createTexture | label_store.set(result, d.label.data, d.label.length);
- runtime/zig/src/native/resource/doe_texture_sampler_native.zig:496 | fn createTexture | label_store.set(result, d.label.data, d.label.length);
- runtime/zig/src/native/resource/doe_texture_sampler_native.zig:678 | pub export fn doeNativeTextureCreateView | label_store.set(result, d.label.data, d.label.length);
- runtime/zig/src/native/resource/doe_texture_sampler_native.zig:694 | pub export fn doeNativeTextureRelease | label_store.remove(raw);
- runtime/zig/src/native/resource/doe_texture_sampler_native.zig:718 | pub export fn doeNativeTextureViewRelease | label_store.remove(raw);
- runtime/zig/src/native/resource/doe_texture_sampler_native.zig:766 | fn createSampler | label_store.set(result, d.label.data, d.label.length);
- runtime/zig/src/native/resource/doe_texture_sampler_native.zig:793 | fn createSampler | label_store.set(result, d.label.data, d.label.length);
- runtime/zig/src/native/resource/doe_texture_sampler_native.zig:803 | fn createSampler | label_store.set(result, d.label.data, d.label.length);
- runtime/zig/src/native/resource/doe_texture_sampler_native.zig:812 | pub export fn doeNativeSamplerRelease | label_store.remove(raw);
- runtime/zig/src/native/shader/doe_shader_native.zig:253 | fn doeNativeDeviceCreateShaderModuleOwned | label_store.set(handle, d.label.data, d.label.length);
- runtime/zig/src/native/shader/doe_shader_native.zig:554 | pub export fn doeNativeShaderModuleRelease | label_store.remove(raw);
- runtime/zig/src/native/shader/doe_shader_native.zig:770 | fn doeNativeDeviceCreateComputePipelineOwned | if (result != null) label_store.set(result, d.label.data, d.label.length);
- runtime/zig/src/native/shader/doe_shader_native.zig:784 | fn doeNativeDeviceCreateComputePipelineOwned | label_store.set(result, d.label.data, d.label.length);
- runtime/zig/src/native/shader/doe_shader_native.zig:874 | fn doeNativeDeviceCreateComputePipelineOwned | label_store.set(result, d.label.data, d.label.length);
- runtime/zig/src/native/shader/doe_shader_native.zig:898 | pub export fn doeNativeComputePipelineRelease | label_store.remove(raw);

## Generic ABI, WebGPU wrappers and shared-memory helper callers

- runtime/zig/src/dropin/dropin_browser_shared_memory.zig:155 | fn labelOwnedObject | fn labelOwnedObject(raw: ?*anyopaque, label: abi_core.WGPUStringView) void {
- runtime/zig/src/dropin/dropin_browser_shared_memory.zig:310 | pub fn wgpuDeviceCreateErrorBuffer | labelOwnedObject(raw, d.label);
- runtime/zig/src/dropin/dropin_browser_shared_memory.zig:346 | pub fn wgpuDeviceCreateErrorTexture | labelOwnedObject(raw, d.label);
- runtime/zig/src/dropin/dropin_browser_shared_memory.zig:379 | pub fn wgpuDeviceImportSharedFence | labelOwnedObject(raw, desc.label);
- runtime/zig/src/dropin/dropin_browser_shared_memory.zig:435 | pub fn wgpuDeviceImportSharedTextureMemory | labelOwnedObject(raw, desc.label);
- runtime/zig/src/dropin/dropin_browser_shared_memory.zig:542 | pub fn wgpuSharedFenceSetLabel | labelOwnedObject(shared_fence, label);
- runtime/zig/src/dropin/dropin_browser_shared_memory.zig:645 | pub fn wgpuSharedTextureMemoryCreateTexture | labelOwnedObject(raw, desc.label);
- runtime/zig/src/dropin/dropin_browser_shared_memory.zig:729 | pub fn wgpuSharedTextureMemorySetLabel | labelOwnedObject(shared_texture_memory, label);
- runtime/zig/src/dropin/wgpu_dropin_ext_b.zig:45 | pub export fn wgpuRenderBundleEncoderSetLabel | native.doeNativeObjectSetLabel(a0, a1.data, a1.length);
- runtime/zig/src/dropin/wgpu_dropin_ext_b.zig:66 | pub export fn wgpuRenderBundleSetLabel | native.doeNativeObjectSetLabel(a0, a1.data, a1.length);
- runtime/zig/src/dropin/wgpu_dropin_ext_c.zig:10 | fn set_local_label | fn set_local_label(handle: ?*anyopaque, label: abi_core.WGPUStringView) void {
- runtime/zig/src/dropin/wgpu_dropin_ext_c.zig:11 | fn set_local_label | native.doeNativeObjectSetLabel(handle, label.data, label.length);
- runtime/zig/src/dropin/wgpu_dropin_ext_c.zig:15 | pub export fn wgpuBindGroupLayoutSetLabel | set_local_label(a0, a1);
- runtime/zig/src/dropin/wgpu_dropin_ext_c.zig:19 | pub export fn wgpuBindGroupSetLabel | set_local_label(a0, a1);
- runtime/zig/src/dropin/wgpu_dropin_ext_c.zig:50 | pub export fn wgpuBufferSetLabel | set_local_label(a0, a1);
- runtime/zig/src/dropin/wgpu_dropin_ext_c.zig:62 | pub export fn wgpuCommandBufferSetLabel | set_local_label(a0, a1);
- runtime/zig/src/dropin/wgpu_dropin_ext_c.zig:80 | pub export fn wgpuCommandEncoderSetLabel | set_local_label(a0, a1);
- runtime/zig/src/dropin/wgpu_dropin_ext_c.zig:96 | pub export fn wgpuComputePassEncoderSetLabel | set_local_label(a0, a1);
- runtime/zig/src/dropin/wgpu_dropin_ext_c.zig:104 | pub export fn wgpuComputePipelineSetLabel | set_local_label(a0, a1);
- runtime/zig/src/dropin/wgpu_dropin_ext_c.zig:121 | pub export fn wgpuDeviceSetLabel | set_local_label(a0, a1);
- runtime/zig/src/dropin/wgpu_dropin_ext_c.zig:146 | pub export fn wgpuPipelineLayoutSetLabel | set_local_label(a0, a1);
- runtime/zig/src/dropin/wgpu_dropin_ext_c.zig:150 | pub export fn wgpuQuerySetSetLabel | set_local_label(a0, a1);
- runtime/zig/src/dropin/wgpu_dropin_ext_c.zig:154 | pub export fn wgpuQueueSetLabel | set_local_label(a0, a1);
- runtime/zig/src/dropin/wgpu_dropin_ext_c.zig:226 | pub export fn wgpuRenderPassEncoderSetLabel | set_local_label(a0, a1);
- runtime/zig/src/dropin/wgpu_dropin_ext_c.zig:230 | pub export fn wgpuRenderPipelineSetLabel | set_local_label(a0, a1);
- runtime/zig/src/dropin/wgpu_dropin_ext_c.zig:234 | pub export fn wgpuSamplerSetLabel | set_local_label(a0, a1);
- runtime/zig/src/dropin/wgpu_dropin_ext_c.zig:238 | pub export fn wgpuShaderModuleSetLabel | set_local_label(a0, a1);
- runtime/zig/src/dropin/wgpu_dropin_ext_c.zig:242 | pub export fn wgpuSurfaceSetLabel | set_local_label(a0, a1);
- runtime/zig/src/dropin/wgpu_dropin_ext_c.zig:246 | pub export fn wgpuTextureSetLabel | set_local_label(a0, a1);
- runtime/zig/src/dropin/wgpu_dropin_ext_c.zig:250 | pub export fn wgpuTextureViewSetLabel | set_local_label(a0, a1);
- runtime/zig/src/native/support/doe_label_store.zig:54 | pub export fn doeNativeObjectSetLabel | pub export fn doeNativeObjectSetLabel(handle: ?*anyopaque, data: ?[*]const u8, len: usize) callconv(.c) void {
- runtime/zig/src/native/support/doe_label_store.zig:61 | pub export fn doeNativeObjectGetLabel | pub export fn doeNativeObjectGetLabel(handle: ?*anyopaque, out_len: ?*usize) callconv(.c) ?[*]const u8 {
- runtime/zig/src/native/support/doe_label_store.zig:69 | pub export fn doeNativeObjectRemoveLabel | pub export fn doeNativeObjectRemoveLabel(handle: ?*anyopaque) callconv(.c) void {

## Real package / C adapter consumers

- runtime/bridge/webgpu-addon/doe_napi_caps.c:727: napi_value doe_object_set_label(napi_env env, napi_callback_info info) {
- runtime/bridge/webgpu-addon/doe_napi_caps.c:730: if (!handle || !pfn_doeNativeObjectSetLabel) return NULL;
- runtime/bridge/webgpu-addon/doe_napi_caps.c:733: pfn_doeNativeObjectSetLabel(handle, (const uint8_t*)label, label_len);
- runtime/bridge/webgpu-addon/doe_napi_globals.c:165: FnObjectSetLabel pfn_doeNativeObjectSetLabel = NULL;
- runtime/bridge/webgpu-addon/doe_napi_globals.c:403: pfn_doeNativeObjectSetLabel = (FnObjectSetLabel)LIB_SYM(g_lib, "doeNativeObjectSetLabel");
- runtime/bridge/webgpu-addon/doe_napi_init.c:196: napi_value doe_object_set_label(napi_env env, napi_callback_info info);
- runtime/bridge/webgpu-addon/doe_napi_init.c:359: EXPORT_FN("objectSetLabel",                           doe_object_set_label),
- runtime/bridge/webgpu-addon/doe_napi_instance.c:489: if (pfn_doeNativeObjectSetLabel && descriptor.label.data && descriptor.label.length > 0) {
- runtime/bridge/webgpu-addon/doe_napi_instance.c:490: pfn_doeNativeObjectSetLabel(result.device, (const uint8_t*)descriptor.label.data, descriptor.label.length);
- runtime/bridge/webgpu-addon/doe_napi_instance.c:492: if (pfn_doeNativeObjectSetLabel && descriptor.defaultQueue.label.data && descriptor.defaultQueue.label.length > 0) {
- runtime/bridge/webgpu-addon/doe_napi_instance.c:495: pfn_doeNativeObjectSetLabel(queue, (const uint8_t*)descriptor.defaultQueue.label.data, descriptor.defaultQueue.label.length);
- runtime/bridge/webgpu-addon/doe_napi_internal.h:668: extern FnObjectSetLabel pfn_doeNativeObjectSetLabel;
- packages/doe-gpu/src/vendor/webgpu/index.js:2720: if (descriptor.label && typeof addon.objectSetLabel === 'function') {
- packages/doe-gpu/src/vendor/webgpu/index.js:2721: addon.objectSetLabel(querySet, descriptor.label);

## Explicit exclusions and cleanup findings

- Surface: wgpuSurfaceSetLabel -> generic map. doeNativeSurfaceRelease has no remove before surface allocator destroy. Proposed exact insertion is documented in integration/CONTRACT.md; old-source failure must be independently shown.
- External texture: wgpuExternalTextureSetLabel -> doeNativeExternalTextureSetLabel, currently a no-op after object cast; its release has no generic-map removal. Generic object-label use on such a key is a separate inherited cleanup gap.
- SharedBufferMemorySetLabel is a no-op. SharedFence/SharedTextureMemory setters and finalizers DO use the map through native.label_store.
- N-API doe_device_get_label/doe_device_set_label are stubs; doe_object_set_label and request-device/default-queue descriptors actually invoke the generic symbol. Package adoption remains untested.
- Known direct existing label test: runtime/c/instance/independent_tests/tests/native_labels.c, 64 actual instance lifetimes with copied binary bytes and alternating surface-held leases. It does not label the surface, inject label OOM, race map operations, or establish borrowed-pointer safety.
- The accepted C-instance callback instanceRemoveLabel -> selected native_helpers.label_store.remove follows accepted queue teardown and precedes clearing instance magic/free. Do not substitute a test callback for this route.
- Descriptor label omissions in unrelated creation owners are not silently repaired or counted as C store implementation. All observed actual set/remove sites above are preserved by the single import seam.
