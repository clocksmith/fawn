#ifndef DOE_VK_DRAW_INDIRECT_INTERNAL_H
#define DOE_VK_DRAW_INDIRECT_INTERNAL_H
#include "render_internal.h"
#include "../include/doe_vk_indirect.h"
struct doe_vk_draw_validator {
    uint32_t references;
    doe_vk_pipeline *pipeline;
};
#endif
