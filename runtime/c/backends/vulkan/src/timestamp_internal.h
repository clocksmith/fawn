#ifndef DOE_VK_TIMESTAMP_INTERNAL_H
#define DOE_VK_TIMESTAMP_INTERNAL_H
#include "query_internal.h"
#include "../include/doe_vk_timestamp.h"
struct doe_vk_timestamp_normalizer {
    uint32_t references;
    uint32_t capacity;
    uint32_t workgroup_size;
    doe_vk_pipeline *pipeline;
    doe_vk_buffer *scratch;
};
#endif
