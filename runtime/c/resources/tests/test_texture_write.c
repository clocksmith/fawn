/* SPDX-License-Identifier: MIT */
#include "fixture.h"

static doe_resource_texture *texture(fixture *f, native_token *token, uint64_t usage)
{
    doe_resource_config c = config(f);
    doe_resource_texture_desc desc = texture_desc();
    doe_resource_texture_limits limits = texture_limits();
    doe_resource_texture *result = NULL;
    desc.usage = usage;
    assert(doe_resource_texture_create(&c, &desc, &limits, native(f, token), &result) == DOE_RESOURCE_OK);
    return result;
}

static void rejected(const void *device, const doe_resource_texture *image, uint64_t data_size,
    const doe_resource_texture_copy *copy, doe_resource_status expected)
{
    doe_resource_copy_region out, untouched;
    memset(&out, 0xa5, sizeof(out));
    memcpy(&untouched, &out, sizeof(out));
    assert(doe_resource_texture_write_validate(device, image, data_size, copy, &out) == expected);
    assert(!memcmp(&out, &untouched, sizeof(out)));
}

static void allocation_free_admission(void)
{
    fixture f = fresh(), other = fresh();
    native_token valid_token = {0}, sampled_token = {0};
    doe_resource_texture *valid = texture(&f, &valid_token, WGPUTextureUsage_CopyDst);
    doe_resource_texture *sampled = texture(&f, &sampled_token, WGPUTextureUsage_TextureBinding);
    const doe_resource_texture_copy zero[] = {
        {.width=0,.height=1,.depth_or_layers=1,.bytes_per_row=UINT32_MAX,.rows_per_image=UINT32_MAX},
        {.width=1,.height=0,.depth_or_layers=1,.bytes_per_row=UINT32_MAX,.rows_per_image=UINT32_MAX},
        {.width=1,.height=1,.depth_or_layers=0,.bytes_per_row=UINT32_MAX,.rows_per_image=UINT32_MAX}
    };
    const size_t allocations = f.allocations, live = f.live;
    const unsigned references = f.device_references;
    f.fail_at = allocations;
    doe_resource_copy_region out;
    for (size_t i = 0; i < sizeof(zero) / sizeof(zero[0]); ++i) {
        assert(doe_resource_texture_write_validate(&f, valid, 0, &zero[i], &out) == DOE_RESOURCE_OK);
        assert(out.required_bytes == 0);
        rejected(&f, sampled, 0, &zero[i], DOE_RESOURCE_USAGE);
        rejected(&other, valid, 0, &zero[i], DOE_RESOURCE_DEVICE_MISMATCH);
    }
    doe_resource_texture_copy copy = zero[0];
    copy.width = 1;
    assert(doe_resource_texture_write_validate(&f, valid, 4, &copy, &out) == DOE_RESOURCE_OK);
    assert(out.required_bytes == 4 && out.row_bytes == 4);
    rejected(&f, valid, 3, &copy, DOE_RESOURCE_RANGE);
    copy.mip = 32;
    rejected(&f, valid, 4, &copy, DOE_RESOURCE_RANGE);
    rejected(&f, sampled, 4, &copy, DOE_RESOURCE_USAGE);
    rejected(&other, sampled, 4, &copy, DOE_RESOURCE_DEVICE_MISMATCH);
    rejected(NULL, valid, 0, &zero[0], DOE_RESOURCE_INVALID_ARGUMENT);
    rejected(&f, NULL, 0, &zero[0], DOE_RESOURCE_DEVICE_MISMATCH);
    rejected(&f, valid, 0, NULL, DOE_RESOURCE_INVALID_ARGUMENT);
    assert(doe_resource_texture_write_validate(&f, valid, 0, &zero[0], NULL) == DOE_RESOURCE_INVALID_ARGUMENT);
    doe_resource_texture_destroy(valid);
    doe_resource_texture_destroy(sampled);
    rejected(&f, valid, 0, &zero[0], DOE_RESOURCE_UNAVAILABLE);
    rejected(&f, sampled, 0, &zero[0], DOE_RESOURCE_UNAVAILABLE);
    rejected(&other, sampled, 0, &zero[0], DOE_RESOURCE_DEVICE_MISMATCH);
    assert(f.allocations == allocations && f.live == live && f.device_references == references);
    /* Prove that the allocator failure was armed, not merely unused configuration. */
    doe_resource_config c = config(&f);
    doe_resource_encoder *encoder = NULL;
    assert(doe_resource_encoder_create(&c, &encoder) == DOE_RESOURCE_OUT_OF_MEMORY && !encoder);
    f.fail_at = SIZE_MAX;
    doe_resource_texture_release(valid);
    doe_resource_texture_release(sampled);
    assert(valid_token.releases == 1 && sampled_token.releases == 1);
    empty(&f);
    empty(&other);
}

static void encoding_liveness_and_first_error(void)
{
    fixture f = fresh();
    doe_resource_config c = config(&f);
    native_token valid_token = {0}, sampled_token = {0}, buffer_token = {0};
    doe_resource_texture *valid = texture(&f, &valid_token, WGPUTextureUsage_CopyDst);
    doe_resource_texture *sampled = texture(&f, &sampled_token, WGPUTextureUsage_TextureBinding);
    doe_resource_buffer *source = buffer(&f, &buffer_token, 4, WGPUBufferUsage_CopySrc);
    const doe_resource_texture_copy copy = {.height=1,.depth_or_layers=1,
        .bytes_per_row=UINT32_MAX,.rows_per_image=UINT32_MAX};
    doe_resource_encoder *encoder = NULL;
    doe_resource_commands *commands = NULL;
    doe_resource_texture_destroy(valid);
    assert(doe_resource_encoder_create(&c, &encoder) == DOE_RESOURCE_OK);
    assert(doe_resource_encoder_copy_texture(encoder, source, valid, &copy,
        DOE_RESOURCE_BUFFER_TO_TEXTURE, DOE_RESOURCE_COPY_WEBGPU) == DOE_RESOURCE_OK);
    assert(doe_resource_encoder_finish(encoder, &commands) == DOE_RESOURCE_OK);
    doe_resource_encoder_release(encoder);
    assert(doe_resource_commands_begin_submit(commands) == DOE_RESOURCE_UNAVAILABLE);
    doe_resource_commands_release(commands);
    assert(doe_resource_encoder_create(&c, &encoder) == DOE_RESOURCE_OK);
    assert(doe_resource_encoder_copy_texture(encoder, source, sampled, &copy,
        DOE_RESOURCE_BUFFER_TO_TEXTURE, DOE_RESOURCE_COPY_WEBGPU) == DOE_RESOURCE_USAGE);
    assert(doe_resource_encoder_copy_texture(encoder, NULL, NULL, NULL,
        DOE_RESOURCE_BUFFER_TO_TEXTURE, DOE_RESOURCE_COPY_WEBGPU) == DOE_RESOURCE_USAGE);
    commands = NULL;
    assert(doe_resource_encoder_finish(encoder, &commands) == DOE_RESOURCE_USAGE && !commands);
    doe_resource_encoder_release(encoder);
    doe_resource_buffer_release(source);
    doe_resource_texture_release(sampled);
    doe_resource_texture_release(valid);
    empty(&f);
}

int main(void)
{
    allocation_free_admission();
    encoding_liveness_and_first_error();
    puts("PASS allocation-free queue texture admission, empty usage validation and encoding liveness/first-error");
    return 0;
}
