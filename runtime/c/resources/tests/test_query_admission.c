/* SPDX-License-Identifier: MIT */
#include "fixture.h"

int main(void)
{
    fixture f = fresh();
    doe_resource_config c = config(&f);
    native_token token = {0}, buffer_token = {0};
    assert(doe_resource_query_set_validate(WGPUQueryType_Timestamp, 0) == DOE_RESOURCE_OK);
    assert(doe_resource_query_set_validate(WGPUQueryType_Occlusion, DOE_RESOURCE_MAX_QUERIES) == DOE_RESOURCE_OK);
    assert(doe_resource_query_set_validate(WGPUQueryType_Timestamp, DOE_RESOURCE_MAX_QUERIES + 1) == DOE_RESOURCE_RANGE);
    assert(doe_resource_query_set_validate(WGPUQueryType_Occlusion, UINT32_MAX) == DOE_RESOURCE_RANGE);
    assert(doe_resource_query_set_validate((WGPUQueryType)0, 1) == DOE_RESOURCE_UNSUPPORTED);
    doe_resource_query_set *sentinel = (void *)(uintptr_t)1;
    size_t allocations = f.allocations;
    assert(doe_resource_query_set_create(&c, WGPUQueryType_Timestamp, DOE_RESOURCE_MAX_QUERIES + 1,
        native(&f, &token), &sentinel) == DOE_RESOURCE_RANGE);
    assert(sentinel == (void *)(uintptr_t)1 && f.allocations == allocations && !token.releases);
    doe_resource_query_set *empty_set;
    assert(doe_resource_query_set_create(&c, WGPUQueryType_Timestamp, 0,
        (doe_resource_native){0}, &empty_set) == DOE_RESOURCE_OK);
    assert(doe_resource_query_set_count(empty_set) == 0 && !doe_resource_query_set_native(empty_set));
    assert(doe_resource_query_set_device(empty_set) == &f);
    doe_resource_buffer *dst = buffer(&f, &buffer_token, 512, WGPUBufferUsage_QueryResolve);
    doe_resource_encoder *encoder;
    assert(doe_resource_encoder_create(&c, &encoder) == DOE_RESOURCE_OK);
    assert(doe_resource_encoder_write_timestamp(encoder, empty_set, 0, DOE_TIMESTAMP_COMMAND) == DOE_RESOURCE_RANGE);
    doe_resource_encoder_release(encoder);
    assert(doe_resource_encoder_create(&c, &encoder) == DOE_RESOURCE_OK);
    assert(doe_resource_encoder_resolve_queries(encoder, empty_set, 0, 0, dst, 0) == DOE_RESOURCE_RANGE);
    doe_resource_encoder_release(encoder);
    doe_resource_query_set_destroy(empty_set);
    doe_resource_query_set_release(empty_set);
    doe_resource_query_set *set;
    assert(doe_resource_query_set_create(&c, WGPUQueryType_Timestamp, DOE_RESOURCE_MAX_QUERIES,
        native(&f, &token), &set) == DOE_RESOURCE_OK);
    assert(doe_resource_encoder_create(&c, &encoder) == DOE_RESOURCE_OK);
    assert(doe_resource_encoder_resolve_queries(encoder, set, DOE_RESOURCE_MAX_QUERIES, 0, dst, 0) == DOE_RESOURCE_RANGE);
    doe_resource_encoder_release(encoder);
    assert(doe_resource_encoder_create(&c, &encoder) == DOE_RESOURCE_OK);
    assert(doe_resource_encoder_resolve_queries(encoder, set, DOE_RESOURCE_MAX_QUERIES - 1, 0, dst, 0) == DOE_RESOURCE_OK);
    doe_resource_encoder_release(encoder);
    doe_resource_query_set_release(set);
    doe_resource_buffer_release(dst);
    assert(token.releases == 1 && buffer_token.releases == 1);
    empty(&f);
    puts("query descriptor owner: 1 host suite passed; no GPU execution claimed");
    return 0;
}
