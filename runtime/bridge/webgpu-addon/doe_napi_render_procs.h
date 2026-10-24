/* SPDX-License-Identifier: MIT */
#ifndef DOE_NAPI_RENDER_PROCS_H
#define DOE_NAPI_RENDER_PROCS_H

/* Private declarations included after the WGPU types and DECL_PFN macro
 * in doe_napi_internal.h. The provider loader owns these optional procedures. */
/* Standard render pass state for providers without the Doe entries. */
DECL_PFN(void, wgpuRenderPassEncoderSetViewport, (WGPURenderPassEncoder, float, float, float, float, float, float));
DECL_PFN(void, wgpuRenderPassEncoderSetScissorRect, (WGPURenderPassEncoder, uint32_t, uint32_t, uint32_t, uint32_t));
DECL_PFN(void, wgpuRenderPassEncoderSetBlendConstant, (WGPURenderPassEncoder, const WGPUColor*));
DECL_PFN(void, wgpuRenderPassEncoderSetStencilReference, (WGPURenderPassEncoder, uint32_t));
DECL_PFN(void, wgpuRenderPassEncoderBeginOcclusionQuery, (WGPURenderPassEncoder, uint32_t));
DECL_PFN(void, wgpuRenderPassEncoderEndOcclusionQuery, (WGPURenderPassEncoder));
DECL_PFN(void, wgpuRenderPassEncoderPushDebugGroup, (WGPURenderPassEncoder, WGPUStringView));
DECL_PFN(void, wgpuRenderPassEncoderPopDebugGroup, (WGPURenderPassEncoder));
DECL_PFN(void, wgpuRenderPassEncoderInsertDebugMarker, (WGPURenderPassEncoder, WGPUStringView));

#endif
