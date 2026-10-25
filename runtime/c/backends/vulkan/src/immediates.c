/* SPDX-License-Identifier: MIT */
#include "internal.h"

void doe_vk_push_immediates(doe_vk_commands *commands, doe_vk_pipeline *pipeline, const void *bytes) {
    if (pipeline->immediate_bytes) commands->context->vk.vkCmdPushConstants(commands->command,
        pipeline->layout,pipeline->immediate_stages,0,pipeline->immediate_bytes,bytes);
}

doe_vk_status doe_vk_commands_set_immediates(doe_vk_commands *commands,
    doe_vk_pipeline *pipeline, const void *bytes, uint32_t size) {
    doe_vk_status status=doe_vk_recording(commands);
    if (status != DOE_VK_OK) return status;
    if (!pipeline || pipeline->context != commands->context || size != pipeline->immediate_bytes ||
        size>64 || size%4 || (size && !bytes)) return doe_vk_poison(commands,DOE_VK_INVALID_ARGUMENT);
    if (!size) return DOE_VK_OK;
    if (!commands->context->vk.vkCmdPushConstants) return doe_vk_poison(commands,DOE_VK_UNSUPPORTED);
    /* Reuse the existing dispatch lease list, not a second pipeline registry.
     * A push-only recording also keeps its layout alive through retirement. */
    bool retained=false;
    for (doe_vk_dispatch_use *use=commands->dispatches;use;use=use->next)
        if (use->pipeline==pipeline) { retained=true;break; }
    if (!retained) {
        doe_vk_dispatch_use *use=calloc(1,sizeof(*use));
        if (!use) return doe_vk_poison(commands,DOE_VK_OUT_OF_MEMORY);
        status=doe_vk_pipeline_retain(pipeline);
        if (status != DOE_VK_OK) { free(use);return doe_vk_poison(commands,status); }
        use->pipeline=pipeline;use->next=commands->dispatches;commands->dispatches=use;
    }
    doe_vk_push_immediates(commands,pipeline,bytes);
    return DOE_VK_OK;
}
