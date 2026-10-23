/* SPDX-License-Identifier: MIT */
/* Exact selected retained vk_structs.zig records, including unions. */
#ifndef DOE_VK_ABI_RENDER_H
#define DOE_VK_ABI_RENDER_H
#include "vk_abi_structs.h"

/* Exact retained vk_render_pipeline.zig declarations. */
typedef struct VkStencilOpState {
    uint32_t failOp, passOp, depthFailOp, compareOp;
    uint32_t compareMask, writeMask, reference;
} VkStencilOpState;
typedef struct VkPipelineDepthStencilStateCreateInfo {
    int32_t sType;
    const void *pNext;
    uint32_t flags, depthTestEnable, depthWriteEnable, depthCompareOp;
    uint32_t depthBoundsTestEnable, stencilTestEnable;
    VkStencilOpState front, back;
    float minDepthBounds, maxDepthBounds;
} VkPipelineDepthStencilStateCreateInfo;

typedef struct VkAttachmentDescription {
    VkFlags flags;
    uint32_t format;
    uint32_t samples;
    uint32_t loadOp;
    uint32_t storeOp;
    uint32_t stencilLoadOp;
    uint32_t stencilStoreOp;
    uint32_t initialLayout;
    uint32_t finalLayout;
} VkAttachmentDescription;

typedef struct VkAttachmentReference {
    uint32_t attachment;
    uint32_t layout;
} VkAttachmentReference;

typedef struct VkSubpassDescription {
    VkFlags flags;
    int32_t pipelineBindPoint;
    uint32_t inputAttachmentCount;
    const VkAttachmentReference *pInputAttachments;
    uint32_t colorAttachmentCount;
    const VkAttachmentReference *pColorAttachments;
    const VkAttachmentReference *pResolveAttachments;
    const VkAttachmentReference *pDepthStencilAttachment;
    uint32_t preserveAttachmentCount;
    const uint32_t *pPreserveAttachments;
} VkSubpassDescription;

typedef struct VkSubpassDependency {
    uint32_t srcSubpass;
    uint32_t dstSubpass;
    VkFlags srcStageMask;
    VkFlags dstStageMask;
    VkFlags srcAccessMask;
    VkFlags dstAccessMask;
    VkFlags dependencyFlags;
} VkSubpassDependency;

typedef struct VkRenderPassCreateInfo {
    VkStructureType sType;
    const void *pNext;
    VkFlags flags;
    uint32_t attachmentCount;
    const VkAttachmentDescription *pAttachments;
    uint32_t subpassCount;
    const VkSubpassDescription *pSubpasses;
    uint32_t dependencyCount;
    const VkSubpassDependency *pDependencies;
} VkRenderPassCreateInfo;

typedef struct VkFramebufferCreateInfo {
    VkStructureType sType;
    const void *pNext;
    VkFlags flags;
    VkRenderPass renderPass;
    uint32_t attachmentCount;
    const VkImageView *pAttachments;
    uint32_t width;
    uint32_t height;
    uint32_t layers;
} VkFramebufferCreateInfo;

typedef struct VkOffset2D {
    int32_t x;
    int32_t y;
} VkOffset2D;

typedef struct VkExtent2D {
    uint32_t width;
    uint32_t height;
} VkExtent2D;

typedef struct VkRect2D {
    VkOffset2D offset;
    VkExtent2D extent;
} VkRect2D;

typedef union VkClearColorValue {
    float float32[4];
    int32_t int32[4];
    uint32_t uint32[4];
} VkClearColorValue;

typedef struct VkClearDepthStencilValue {
    float depth;
    uint32_t stencil;
} VkClearDepthStencilValue;

typedef union VkClearValue {
    VkClearColorValue color;
    VkClearDepthStencilValue depthStencil;
} VkClearValue;

typedef struct VkRenderPassBeginInfo {
    VkStructureType sType;
    const void *pNext;
    VkRenderPass renderPass;
    VkFramebuffer framebuffer;
    VkRect2D renderArea;
    uint32_t clearValueCount;
    const VkClearValue *pClearValues;
} VkRenderPassBeginInfo;

typedef struct VkViewport {
    float x;
    float y;
    float width;
    float height;
    float minDepth;
    float maxDepth;
} VkViewport;

typedef struct VkVertexInputBindingDescription {
    uint32_t binding;
    uint32_t stride;
    uint32_t inputRate;
} VkVertexInputBindingDescription;

typedef struct VkVertexInputAttributeDescription {
    uint32_t location;
    uint32_t binding;
    uint32_t format;
    uint32_t offset;
} VkVertexInputAttributeDescription;

typedef struct VkPipelineVertexInputStateCreateInfo {
    VkStructureType sType;
    const void *pNext;
    VkFlags flags;
    uint32_t vertexBindingDescriptionCount;
    const VkVertexInputBindingDescription *pVertexBindingDescriptions;
    uint32_t vertexAttributeDescriptionCount;
    const VkVertexInputAttributeDescription *pVertexAttributeDescriptions;
} VkPipelineVertexInputStateCreateInfo;

typedef struct VkPipelineInputAssemblyStateCreateInfo {
    VkStructureType sType;
    const void *pNext;
    VkFlags flags;
    uint32_t topology;
    VkBool32 primitiveRestartEnable;
} VkPipelineInputAssemblyStateCreateInfo;

typedef struct VkPipelineViewportStateCreateInfo {
    VkStructureType sType;
    const void *pNext;
    VkFlags flags;
    uint32_t viewportCount;
    const VkViewport *pViewports;
    uint32_t scissorCount;
    const VkRect2D *pScissors;
} VkPipelineViewportStateCreateInfo;

typedef struct VkPipelineRasterizationStateCreateInfo {
    VkStructureType sType;
    const void *pNext;
    VkFlags flags;
    VkBool32 depthClampEnable;
    VkBool32 rasterizerDiscardEnable;
    uint32_t polygonMode;
    VkFlags cullMode;
    uint32_t frontFace;
    VkBool32 depthBiasEnable;
    float depthBiasConstantFactor;
    float depthBiasClamp;
    float depthBiasSlopeFactor;
    float lineWidth;
} VkPipelineRasterizationStateCreateInfo;

typedef struct VkPipelineMultisampleStateCreateInfo {
    VkStructureType sType;
    const void *pNext;
    VkFlags flags;
    uint32_t rasterizationSamples;
    VkBool32 sampleShadingEnable;
    float minSampleShading;
    const uint32_t *pSampleMask;
    VkBool32 alphaToCoverageEnable;
    VkBool32 alphaToOneEnable;
} VkPipelineMultisampleStateCreateInfo;

typedef struct VkPipelineColorBlendAttachmentState {
    VkBool32 blendEnable;
    uint32_t srcColorBlendFactor;
    uint32_t dstColorBlendFactor;
    uint32_t colorBlendOp;
    uint32_t srcAlphaBlendFactor;
    uint32_t dstAlphaBlendFactor;
    uint32_t alphaBlendOp;
    VkFlags colorWriteMask;
} VkPipelineColorBlendAttachmentState;

typedef struct VkPipelineColorBlendStateCreateInfo {
    VkStructureType sType;
    const void *pNext;
    VkFlags flags;
    VkBool32 logicOpEnable;
    uint32_t logicOp;
    uint32_t attachmentCount;
    const VkPipelineColorBlendAttachmentState *pAttachments;
    float blendConstants[4];
} VkPipelineColorBlendStateCreateInfo;

typedef struct VkPipelineDynamicStateCreateInfo {
    VkStructureType sType;
    const void *pNext;
    VkFlags flags;
    uint32_t dynamicStateCount;
    const uint32_t *pDynamicStates;
} VkPipelineDynamicStateCreateInfo;

typedef struct VkGraphicsPipelineCreateInfo {
    VkStructureType sType;
    const void *pNext;
    VkFlags flags;
    uint32_t stageCount;
    const VkPipelineShaderStageCreateInfo *pStages;
    const VkPipelineVertexInputStateCreateInfo *pVertexInputState;
    const VkPipelineInputAssemblyStateCreateInfo *pInputAssemblyState;
    const void *pTessellationState;
    const VkPipelineViewportStateCreateInfo *pViewportState;
    const VkPipelineRasterizationStateCreateInfo *pRasterizationState;
    const VkPipelineMultisampleStateCreateInfo *pMultisampleState;
    const void *pDepthStencilState;
    const VkPipelineColorBlendStateCreateInfo *pColorBlendState;
    const VkPipelineDynamicStateCreateInfo *pDynamicState;
    VkPipelineLayout layout;
    VkRenderPass renderPass;
    uint32_t subpass;
    VkPipeline basePipelineHandle;
    int32_t basePipelineIndex;
} VkGraphicsPipelineCreateInfo;

#endif
