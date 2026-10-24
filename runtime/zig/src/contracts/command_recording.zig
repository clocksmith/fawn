pub const BufferCopyError = error{
    InvalidArgument,
    BufferCopyDeviceMismatch,
    BufferCopyUsageMissing,
    BufferCopyUnaligned,
    BufferCopyOutOfBounds,
    BufferCopyAliasing,
};

pub const RenderAttachmentError = error{
    RenderAttachmentUnavailable,
    RenderAttachmentDeviceMismatch,
    RenderAttachmentUsageMissing,
    RenderAttachmentViewInvalid,
    RenderAttachmentExtentMismatch,
    RenderAttachmentSampleMismatch,
    RenderAttachmentFormatMismatch,
    RenderAttachmentUnsupported,
    RenderAttachmentColorBytesExceeded,
};

/// WebGPU draw-time buffer validation
/// (https://gpuweb.github.io/gpuweb/#abstract-opdef-validate-draw).
pub const DrawError = error{
    DrawVertexBufferMissing,
    DrawVertexBufferOutOfBounds,
    DrawIndexBufferMissing,
    DrawIndexBufferOutOfBounds,
    DrawBindGroupMissing,
    DrawBindGroupIncompatible,
    DrawBindingUnsupported,
    DrawCountExceeded,
};

/// WebGPU setBindGroup dynamic offsets
/// (https://gpuweb.github.io/gpuweb/#dom-gpubindingcommandsmixin-setbindgroup).
pub const BindGroupError = error{
    BindGroupIndexOutOfRange,
    BindGroupDynamicOffsetCount,
    BindGroupDynamicOffsetUnaligned,
    BindGroupDynamicOffsetOutOfBounds,
};

/// WebGPU dispatch validation
/// (https://gpuweb.github.io/gpuweb/#dom-gpucomputepassencoder-dispatchworkgroups).
pub const DispatchError = error{
    DispatchPipelineMissing,
    DispatchWorkgroupCountExceeded,
    DispatchBindGroupMissing,
    DispatchBindGroupIncompatible,
    DispatchBindingUnsupported,
};

pub const Failure = RenderAttachmentError || BufferCopyError || DrawError || BindGroupError || DispatchError || @import("texture_copy.zig").Error || error{ OutOfMemory, InvalidState, ImmediateDataUnsupported, ImmediateDataRange, DebugGroupUnbalanced, TextureCopyDeviceMismatch, TextureCopyUsageMissing };

pub fn message(cause: Failure) []const u8 {
    return switch (cause) {
        error.RenderAttachmentUnavailable => "render attachment requires a live texture view and pipeline",
        error.RenderAttachmentDeviceMismatch => "render attachments and pipeline must belong to the encoder device",
        error.RenderAttachmentUsageMissing => "render attachment view requires RENDER_ATTACHMENT usage",
        error.RenderAttachmentViewInvalid => "render attachment requires a single mip and layer with all format aspects",
        error.RenderAttachmentExtentMismatch => "render color and depth attachment mip extents must match",
        error.RenderAttachmentSampleMismatch => "render attachments and pipeline sample counts must match",
        error.RenderAttachmentFormatMismatch => "render attachment formats must match their roles and pipeline targets",
        error.RenderAttachmentUnsupported => "Vulkan rendering supports up to eight 2D color attachments and an optional depth/stencil attachment",
        error.RenderAttachmentColorBytesExceeded => "render pass color attachments exceed maxColorAttachmentBytesPerSample",
        error.DrawVertexBufferMissing => "draw requires a vertex buffer in every slot the pipeline uses",
        error.DrawVertexBufferOutOfBounds => "draw reads past the bound vertex buffer range",
        error.DrawIndexBufferMissing => "drawIndexed requires a bound index buffer",
        error.DrawIndexBufferOutOfBounds => "drawIndexed reads past the bound index buffer range",
        error.DrawBindGroupMissing => "draw requires a bind group in every group the pipeline uses",
        error.DrawBindGroupIncompatible => "draw bind group layout is not compatible with the pipeline layout",
        error.DrawBindingUnsupported => "draw pipeline uses a binding number above 31, which this provider validates but cannot execute",
        error.DrawCountExceeded => "render pass draws exceed its maxDrawCount",
        error.BindGroupIndexOutOfRange => "setBindGroup index must be less than maxBindGroups",
        error.BindGroupDynamicOffsetCount => "setBindGroup dynamic offset count must match the layout's dynamic bindings",
        error.BindGroupDynamicOffsetUnaligned => "setBindGroup dynamic offsets must meet the minimum buffer offset alignment",
        error.BindGroupDynamicOffsetOutOfBounds => "setBindGroup dynamic offset moves a binding past its buffer",
        error.DispatchPipelineMissing => "dispatch requires a compute pipeline",
        error.DispatchWorkgroupCountExceeded => "dispatch workgroup count exceeds maxComputeWorkgroupsPerDimension",
        error.DispatchBindGroupMissing => "dispatch requires a bind group in every group the pipeline uses",
        error.DispatchBindGroupIncompatible => "dispatch bind group layout is not compatible with the pipeline layout",
        error.DispatchBindingUnsupported => "dispatch pipeline uses a binding number above 31, which this provider validates but cannot execute",
        error.OutOfMemory => "GPU command recording could not allocate owned storage",
        error.InvalidState => "GPU command recording requires an open encoder or its active pass",
        error.InvalidArgument => "GPU command recording received an invalid dependency or payload",
        error.BufferCopyDeviceMismatch => "buffer copy requires both buffers to belong to the encoder's device",
        error.BufferCopyUsageMissing => "buffer copy requires COPY_SRC on source and COPY_DST on destination",
        error.BufferCopyUnaligned => "buffer copy offsets and size must be multiples of four bytes",
        error.BufferCopyOutOfBounds => "buffer copy range exceeds the source or destination size",
        error.BufferCopyAliasing => "buffer copy source and destination alias outside the permitted copy contract",
        error.TextureCopyRange => "texture copy mip, origin, extent or buffer range exceeds the resource",
        error.TextureCopyLayout => "texture copy requires aligned offsets and sufficient row/image strides",
        error.TextureCopyAspect => "texture copy requires one valid, copyable format aspect",
        error.TextureCopyUnsupported => "texture copy format or execution backend is unsupported",
        error.TextureCopyDeviceMismatch => "texture copy resources must belong to the encoder's device",
        error.TextureCopyUsageMissing => "texture copy requires COPY_SRC on source and COPY_DST on destination",
        error.ImmediateDataRange => "setImmediates offset and size must be 4-byte aligned and within maxImmediateSize",
        error.DebugGroupUnbalanced => "popDebugGroup needs an open debug group, and every pushDebugGroup must be popped before end() or finish()",
        error.ImmediateDataUnsupported => "setImmediates: shader-visible immediate data is unsupported; use buffer bindings",
    };
}

/// Open debug groups of an encoder, pass or bundle encoder: popDebugGroup
/// needs one open, and end() or finish() needs none
/// (https://gpuweb.github.io/gpuweb/#dom-gpudebugcommandsmixin-popdebuggroup).
pub const DebugGroups = struct {
    depth: u32 = 0,

    pub fn push(self: *DebugGroups) void {
        self.depth +|= 1;
    }

    pub fn pop(self: *DebugGroups) error{DebugGroupUnbalanced}!void {
        if (self.depth == 0) return error.DebugGroupUnbalanced;
        self.depth -= 1;
    }

    pub fn requireBalanced(self: DebugGroups) error{DebugGroupUnbalanced}!void {
        if (self.depth != 0) return error.DebugGroupUnbalanced;
    }
};

pub const State = union(enum) {
    open,
    pass: usize,
    failed: Failure,
    finished,

    pub fn fail(self: *State, cause: Failure) bool {
        if (self.* == .failed) return false;
        self.* = .{ .failed = cause };
        return true;
    }
};
