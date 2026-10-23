/* SPDX-License-Identifier: MIT */
#ifndef DOE_VK_QUERY_INTERNAL_H
#define DOE_VK_QUERY_INTERNAL_H
#include "internal.h"
#include "../include/doe_vk_queries.h"
typedef enum doe_vk_query_kind { DOE_VK_QUERY_TIMESTAMP, DOE_VK_QUERY_OCCLUSION } doe_vk_query_kind;
enum { DOE_VK_QUERY_UNRESET, DOE_VK_QUERY_RESET, DOE_VK_QUERY_WRITTEN };
struct doe_vk_query_pool {
    doe_vk_context *context;
    uint32_t references;
    uint32_t count;
    doe_vk_query_kind kind;
    uint8_t *states;
    doe_vk_status content_status;
    doe_vk_commands *recording_owner;
    VkQueryPool pool;
};
typedef struct doe_vk_query_use {
    doe_vk_query_pool *pool;
    uint8_t *states;
    /* Per index: reset or written by this recording. Completion publishes only
     * these. depends_on_prior marks a write/resolve of an index this recording
     * never reset, whose meaning would follow earlier pool state. */
    uint8_t *touched;
    bool depends_on_prior;
    struct doe_vk_query_use *next;
} doe_vk_query_use;
doe_vk_status doe_vk_use_queries(doe_vk_commands *commands, doe_vk_query_pool *pool,
    doe_vk_query_use **out);
void doe_vk_queries_complete(doe_vk_commands *commands, doe_vk_status status);
void doe_vk_queries_release(doe_vk_commands *commands);
#endif
