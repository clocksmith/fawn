#ifndef DOE_VK_QUERIES_H
#define DOE_VK_QUERIES_H
#include "doe_vk.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct doe_vk_query_pool doe_vk_query_pool;
/* Timestamp-only native owner. Feature admission remains upstream; nonzero
 * queue timestamp_valid_bits and a finite positive timestamp_period_ns are
 * required. Newly allocated entries must be reset before first write. */
doe_vk_status doe_vk_query_pool_create(doe_vk_context *context, uint32_t count,
    doe_vk_query_pool **out, doe_vk_error *error);
doe_vk_status doe_vk_query_pool_retain(doe_vk_query_pool *pool);
void doe_vk_query_pool_release(doe_vk_query_pool *pool);
/* The pool is held by one unresolved command owner. Successful completion
 * publishes reset/written state; terminal failure poisons it (replace the pool).
 * Recorded work retains the pool independently of caller references. */
doe_vk_status doe_vk_commands_reset_queries(doe_vk_commands *commands,
    doe_vk_query_pool *pool, uint32_t first, uint32_t count);
typedef enum doe_vk_timestamp_stage { DOE_VK_TIMESTAMP_BEGIN, DOE_VK_TIMESTAMP_END } doe_vk_timestamp_stage;
/* BEGIN maps TOP_OF_PIPE; END/standalone command maps BOTTOM_OF_PIPE. */
doe_vk_status doe_vk_commands_write_timestamp(doe_vk_commands *commands,
    doe_vk_query_pool *pool, uint32_t index, doe_vk_timestamp_stage stage);
/* Copies written entries as packed uint64_t raw ticks with native WAIT/64-bit
 * flags; reset/never-written entries are filled with zero by native commands,
 * never waited on. Destination needs COPY_DST and an 8-byte-aligned range. Zero
 * count is a validated no-op. Written values are not converted or masked. */
doe_vk_status doe_vk_commands_resolve_queries(doe_vk_commands *commands,
    doe_vk_query_pool *pool, uint32_t first, uint32_t count,
    doe_vk_buffer *destination, uint64_t offset);
#ifdef __cplusplus
}
#endif
#endif
