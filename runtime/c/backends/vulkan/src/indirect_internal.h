/* SPDX-License-Identifier: MIT */
#ifndef DOE_VK_INDIRECT_INTERNAL_H
#define DOE_VK_INDIRECT_INTERNAL_H
#include "internal.h"
#include "../include/doe_vk_indirect.h"
struct doe_vk_dispatch_validator {
    uint32_t references;
    doe_vk_pipeline *pipeline;
    doe_vk_buffer *scratch;
};
#endif
