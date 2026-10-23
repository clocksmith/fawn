/* SPDX-License-Identifier: MIT */
#include "objc_abi.h"

bool doe_mtl_objc_load_render(doe_mtl_objc *r, doe_objc_message message) {
    doe_mtl_objc_graphics *g = &r->graphics;
    g->index_object = (doe_objc_index_object_call)message;
    g->set_bool = (doe_objc_set_bool_call)message;
    g->set_u32 = (doe_objc_set_u32_call)message;
    g->set_double = (doe_objc_set_double_call)message;
    g->clear_color = (doe_objc_clear_color_call)message;
    g->viewport = (doe_objc_viewport_call)message;
    g->scissor = (doe_objc_scissor_call)message;
    g->blend_color = (doe_objc_blend_color_call)message;
    g->depth_bias = (doe_objc_depth_bias_call)message;
    g->draw = (doe_objc_draw_call)message;
    g->draw_indexed = (doe_objc_draw_indexed_call)message;
    g->pipeline_class = r->get_class("MTLRenderPipelineDescriptor");
    g->vertex_class = r->get_class("MTLVertexDescriptor");
    g->pass_class = r->get_class("MTLRenderPassDescriptor");
    g->depth_class = r->get_class("MTLDepthStencilDescriptor");
    g->stencil_class = r->get_class("MTLStencilDescriptor");
    g->new_pipeline = r->selector("newRenderPipelineStateWithDescriptor:error:");
    g->new_depth = r->selector("newDepthStencilStateWithDescriptor:");
    g->new_encoder = r->selector("renderCommandEncoderWithDescriptor:");
    g->set_vertex_function = r->selector("setVertexFunction:");
    g->set_fragment_function = r->selector("setFragmentFunction:");
    g->colors = r->selector("colorAttachments");
    g->index = r->selector("objectAtIndexedSubscript:");
    g->set_raster_samples = r->selector("setRasterSampleCount:");
    g->set_vertex_descriptor = r->selector("setVertexDescriptor:");
    g->set_depth_format = r->selector("setDepthAttachmentPixelFormat:");
    g->set_stencil_format = r->selector("setStencilAttachmentPixelFormat:");
    g->set_blend = r->selector("setBlendingEnabled:");
    g->set_write_mask = r->selector("setWriteMask:");
    g->set_color_op = r->selector("setRgbBlendOperation:");
    g->set_color_source = r->selector("setSourceRGBBlendFactor:");
    g->set_color_destination = r->selector("setDestinationRGBBlendFactor:");
    g->set_alpha_op = r->selector("setAlphaBlendOperation:");
    g->set_alpha_source = r->selector("setSourceAlphaBlendFactor:");
    g->set_alpha_destination = r->selector("setDestinationAlphaBlendFactor:");
    g->layouts = r->selector("layouts");
    g->attributes = r->selector("attributes");
    g->set_stride = r->selector("setStride:");
    g->set_step = r->selector("setStepFunction:");
    g->set_rate = r->selector("setStepRate:");
    g->set_attribute_format = r->selector("setFormat:");
    g->set_offset = r->selector("setOffset:");
    g->set_buffer_index = r->selector("setBufferIndex:");
    g->set_depth_compare = r->selector("setDepthCompareFunction:");
    g->set_depth_write = r->selector("setDepthWriteEnabled:");
    g->set_front_stencil = r->selector("setFrontFaceStencil:");
    g->set_back_stencil = r->selector("setBackFaceStencil:");
    g->set_stencil_compare = r->selector("setStencilCompareFunction:");
    g->set_stencil_fail = r->selector("setStencilFailureOperation:");
    g->set_depth_fail = r->selector("setDepthFailureOperation:");
    g->set_stencil_pass = r->selector("setDepthStencilPassOperation:");
    g->set_read_mask = r->selector("setReadMask:");
    g->set_stencil_write_mask = r->selector("setWriteMask:");
    g->depth_attachment = r->selector("depthAttachment");
    g->stencil_attachment = r->selector("stencilAttachment");
    g->set_texture = r->selector("setTexture:");
    g->set_load = r->selector("setLoadAction:");
    g->set_store = r->selector("setStoreAction:");
    g->set_level = r->selector("setLevel:");
    g->set_slice = r->selector("setSlice:");
    g->set_clear_color = r->selector("setClearColor:");
    g->set_clear_depth = r->selector("setClearDepth:");
    g->set_clear_stencil = r->selector("setClearStencil:");
    g->set_pipeline = r->selector("setRenderPipelineState:");
    g->set_depth_state = r->selector("setDepthStencilState:");
    g->set_vertex_buffer = r->selector("setVertexBuffer:offset:atIndex:");
    g->set_fragment_buffer = r->selector("setFragmentBuffer:offset:atIndex:");
    g->set_vertex_bytes = r->selector("setVertexBytes:length:atIndex:");
    g->set_fragment_bytes = r->selector("setFragmentBytes:length:atIndex:");
    g->set_vertex_texture = r->selector("setVertexTexture:atIndex:");
    g->set_fragment_texture = r->selector("setFragmentTexture:atIndex:");
    g->set_vertex_sampler = r->selector("setVertexSamplerState:atIndex:");
    g->set_fragment_sampler = r->selector("setFragmentSamplerState:atIndex:");
    g->set_viewport = r->selector("setViewport:");
    g->set_scissor = r->selector("setScissorRect:");
    g->set_winding = r->selector("setFrontFacingWinding:");
    g->set_cull = r->selector("setCullMode:");
    g->set_blend_color = r->selector("setBlendColorRed:green:blue:alpha:");
    g->set_depth_bias = r->selector("setDepthBias:slopeScale:clamp:");
    g->set_stencil_reference = r->selector("setStencilReferenceValue:");
    g->draw_primitives = r->selector("drawPrimitives:vertexStart:vertexCount:instanceCount:baseInstance:");
    g->draw_indexed_primitives = r->selector("drawIndexedPrimitives:indexCount:indexType:indexBuffer:indexBufferOffset:instanceCount:baseVertex:baseInstance:");
    return g->pipeline_class != NULL && g->vertex_class != NULL && g->pass_class != NULL &&
        g->depth_class != NULL && g->stencil_class != NULL && g->new_pipeline != NULL &&
        g->new_depth != NULL && g->new_encoder != NULL && g->index != NULL &&
        g->set_vertex_function != NULL && g->set_fragment_function != NULL && g->colors != NULL &&
        g->set_raster_samples != NULL && g->set_vertex_descriptor != NULL && g->set_depth_format != NULL &&
        g->set_stencil_format != NULL && g->set_blend != NULL && g->set_write_mask != NULL &&
        g->set_color_op != NULL && g->set_color_source != NULL && g->set_color_destination != NULL &&
        g->set_alpha_op != NULL && g->set_alpha_source != NULL && g->set_alpha_destination != NULL &&
        g->layouts != NULL && g->attributes != NULL && g->set_stride != NULL && g->set_step != NULL &&
        g->set_rate != NULL && g->set_attribute_format != NULL && g->set_offset != NULL && g->set_buffer_index != NULL &&
        g->set_depth_compare != NULL && g->set_depth_write != NULL && g->set_front_stencil != NULL &&
        g->set_back_stencil != NULL && g->set_stencil_compare != NULL && g->set_stencil_fail != NULL &&
        g->set_depth_fail != NULL && g->set_stencil_pass != NULL && g->set_read_mask != NULL &&
        g->set_stencil_write_mask != NULL && g->depth_attachment != NULL && g->stencil_attachment != NULL &&
        g->set_texture != NULL && g->set_load != NULL && g->set_store != NULL && g->set_level != NULL &&
        g->set_slice != NULL && g->set_clear_color != NULL && g->set_clear_depth != NULL &&
        g->set_clear_stencil != NULL && g->set_pipeline != NULL && g->set_depth_state != NULL &&
        g->set_vertex_buffer != NULL && g->set_fragment_buffer != NULL && g->set_vertex_bytes != NULL &&
        g->set_fragment_bytes != NULL && g->set_vertex_texture != NULL && g->set_fragment_texture != NULL &&
        g->set_vertex_sampler != NULL && g->set_fragment_sampler != NULL && g->set_viewport != NULL &&
        g->set_scissor != NULL && g->set_winding != NULL && g->set_cull != NULL && g->set_blend_color != NULL &&
        g->set_depth_bias != NULL && g->set_stencil_reference != NULL &&
        g->draw_primitives != NULL && g->draw_indexed_primitives != NULL;
}
