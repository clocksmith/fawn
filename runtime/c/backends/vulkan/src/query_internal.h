#ifndef DOE_VK_QUERY_INTERNAL_H
#define DOE_VK_QUERY_INTERNAL_H
#include "internal.h"
#include "../include/doe_vk_queries.h"
enum { DOE_VK_QUERY_UNRESET, DOE_VK_QUERY_RESET, DOE_VK_QUERY_WRITTEN };
struct doe_vk_query_pool {
    doe_vk_context *context;
    uint32_t references;
    uint32_t count;
    uint8_t *states;
    doe_vk_status content_status;
    doe_vk_commands *recording_owner;
    VkQueryPool pool;
};
typedef struct doe_vk_query_use {
    doe_vk_query_pool *pool;
    uint8_t *states;
    struct doe_vk_query_use *next;
} doe_vk_query_use;
doe_vk_status doe_vk_use_queries(doe_vk_commands *commands, doe_vk_query_pool *pool,
    doe_vk_query_use **out);
void doe_vk_queries_complete(doe_vk_commands *commands, doe_vk_status status);
void doe_vk_queries_release(doe_vk_commands *commands);
#endif
