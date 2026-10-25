/* SPDX-License-Identifier: MIT */
#include "../src/internal.h"
#include <assert.h>
#include <stdio.h>
static VkPushConstantRange observed;
static uint32_t pushed[16];
static unsigned layouts, pushes;
static VkResult create_layout(VkDevice device, const VkPipelineLayoutCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkPipelineLayout *out) {
    (void)device;(void)allocator;
    assert(info->pushConstantRangeCount==1 && info->pPushConstantRanges);
    observed=*(const VkPushConstantRange *)info->pPushConstantRanges;
    *out=91;++layouts;return 0;
}
static void push(VkCommandBuffer commands, VkPipelineLayout layout, VkFlags stages,
    uint32_t offset, uint32_t size, const void *bytes) {
    (void)commands;assert(layout==91 && stages==VK_SHADER_STAGE_COMPUTE_BIT && !offset && size==8);
    memcpy(pushed,bytes,size);++pushes;
}
int main(void) {
    doe_vk_context context={.references=1};
    context.properties.limits.maxPushConstantsSize=64;
    context.vk.vkCreatePipelineLayout=create_layout;context.vk.vkCmdPushConstants=push;
    doe_vk_pipeline pipeline={.context=&context,.references=1,.immediate_bytes=8,.immediate_stages=VK_SHADER_STAGE_COMPUTE_BIT};
    assert(doe_vk_pipeline_layout_create(&pipeline,NULL,0,VK_SHADER_STAGE_COMPUTE_BIT,NULL)==DOE_VK_OK);
    assert(layouts==1 && observed.size==8 && observed.offset==0 && observed.stageFlags==VK_SHADER_STAGE_COMPUTE_BIT);
    doe_vk_commands commands={.context=&context,.state=DOE_VK_RECORDING};
    uint32_t words[2]={123,456};
    assert(doe_vk_commands_set_immediates(&commands,&pipeline,words,8)==DOE_VK_OK);
    assert(pushes==1 && pipeline.references==2 && commands.dispatches);
    words[0]=999;assert(pushed[0]==123);
    assert(doe_vk_commands_set_immediates(&commands,&pipeline,words,8)==DOE_VK_OK);
    assert(pushes==2 && pipeline.references==2); /* Same existing lease, not another owner. */
    assert(doe_vk_commands_set_immediates(&commands,&pipeline,words,4)==DOE_VK_INVALID_ARGUMENT);
    assert(pushes==2);
    doe_vk_dispatch_release(&context,commands.dispatches);assert(pipeline.references==1);
    pipeline.immediate_bytes=2;
    assert(doe_vk_pipeline_layout_create(&pipeline,NULL,0,VK_SHADER_STAGE_COMPUTE_BIT,NULL)==DOE_VK_INVALID_ARGUMENT);
    pipeline.immediate_bytes=8;pipeline.immediate_stages=VK_SHADER_STAGE_FRAGMENT_BIT;
    assert(doe_vk_pipeline_layout_create(&pipeline,NULL,0,VK_SHADER_STAGE_COMPUTE_BIT,NULL)==DOE_VK_INVALID_ARGUMENT);
    assert(layouts==1);
    puts("PASS immediate exact native ranges/stages, typed push arguments, existing lease reuse and invalid size/stage refusal; host only");
}
