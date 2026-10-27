/* SPDX-License-Identifier: MIT */
#include "fixture.h"

int main(void)
{
    fixture f = fresh();
    doe_resource_config c = config(&f);
    native_token token = {0};
    doe_resource_buffer_desc descriptor = {16, WGPUBufferUsage_CopyDst, true};
    doe_resource_buffer *destination = NULL;
    assert(doe_resource_buffer_create(&c, &descriptor, native(&f, &token), &destination) == DOE_RESOURCE_OK);
    doe_resource_encoder *encoder = NULL;
    assert(doe_resource_encoder_create(&c, &encoder) == DOE_RESOURCE_OK);
    size_t allocations = f.allocations;
    assert(doe_resource_encoder_write_buffer_validate(encoder, destination, 0, 16) == DOE_RESOURCE_OK);
    assert(doe_resource_encoder_write_buffer_validate(encoder, destination, 16, 0) == DOE_RESOURCE_OK);
    assert(f.allocations == allocations);
    assert(doe_resource_buffer_write_validate(destination, &f, 0, 16) == DOE_RESOURCE_STATE);
    doe_resource_commands *commands = NULL;
    assert(doe_resource_encoder_finish(encoder, &commands) == DOE_RESOURCE_OK);
    size_t count = 123;
    (void)doe_resource_commands_borrow(commands, &count);
    assert(count == 0);
    assert(doe_resource_encoder_write_buffer_validate(encoder, destination, 0, 4) == DOE_RESOURCE_STATE);
    doe_resource_commands_release(commands);
    doe_resource_encoder_release(encoder);
    assert(doe_resource_encoder_create(&c, &encoder) == DOE_RESOURCE_OK);
    allocations = f.allocations;
    assert(doe_resource_encoder_write_buffer_validate(encoder, destination, 1, 4) == DOE_RESOURCE_ALIGNMENT);
    assert(doe_resource_encoder_write_buffer_validate(encoder, destination, 0, 4) == DOE_RESOURCE_ALIGNMENT);
    assert(f.allocations == allocations);
    doe_resource_encoder_release(encoder);
    assert(doe_resource_encoder_create(&c, &encoder) == DOE_RESOURCE_OK);
    assert(doe_resource_encoder_write_buffer_validate(encoder, destination, 16, 4) == DOE_RESOURCE_RANGE);
    doe_resource_encoder_release(encoder);
    doe_resource_buffer_release(destination);
    assert(token.releases == 1);
    empty(&f);
    puts("PASS encoded write preflight: no allocation or commands, submit-time mapping, sticky rejection and full release");
    return 0;
}
