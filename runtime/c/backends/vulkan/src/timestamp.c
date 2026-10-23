#include "timestamp_internal.h"
#include "timestamp_shader.h"
#include <float.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>

_Static_assert(sizeof(float) == sizeof(uint32_t), "timestamp period is binary32");
_Static_assert(FLT_RADIX == 2 && FLT_MANT_DIG == 24 && FLT_MAX_EXP == 128 &&
    FLT_MIN_EXP == -125, "timestamp period requires IEEE binary32 parameters");

doe_vk_status doe_vk_timestamp_shader_source(float period_ns, uint32_t valid_bits,
    uint32_t workgroup_size, char *out, size_t capacity, size_t *out_length) {
    if (!isfinite(period_ns) || period_ns <= 0 || valid_bits == 0 || valid_bits > 64 ||
        workgroup_size == 0 || out == NULL || out_length == NULL)
        return DOE_VK_INVALID_ARGUMENT;
    uint32_t bits;
    memcpy(&bits, &period_ns, sizeof(bits));
    uint32_t exponent = (bits >> 23) & 255;
    uint32_t mantissa = (bits & UINT32_C(0x7fffff)) | (exponent == 0 ? 0 : UINT32_C(1) << 23);
    int32_t shift = exponent == 0 ? -149 : (int32_t)exponent - 150;
    uint64_t mask = UINT64_MAX >> (64 - valid_bits);
    char source[DOE_VK_TIMESTAMP_SOURCE_CAPACITY];
    int length = snprintf(source, sizeof(source),
        "const MANTISSA = %" PRIu32 "u;\nconst PERIOD_SHIFT = %" PRId32 "i;\n"
        "const MASK_LOW = %" PRIu32 "u;\nconst MASK_HIGH = %" PRIu32 "u;\n"
        "const WORKGROUP_SIZE = %" PRIu32 "u;\n%s",
        mantissa, shift, (uint32_t)mask, (uint32_t)(mask >> 32), workgroup_size,
        doe_vk_timestamp_kernel);
    if (length < 0 || (size_t)length >= sizeof(source)) return DOE_VK_INVALID_STATE;
    if ((size_t)length >= capacity) return DOE_VK_INVALID_ARGUMENT;
    memcpy(out, source, (size_t)length + 1);
    *out_length = (size_t)length;
    return DOE_VK_OK;
}

doe_vk_status doe_vk_timestamp_normalizer_create(doe_vk_context *context,
    const doe_vk_timestamp_normalizer_desc *descriptor,
    doe_vk_timestamp_normalizer **out, doe_vk_error *error) {
    if (context == NULL || descriptor == NULL || out == NULL ||
        descriptor->capacity == 0 || descriptor->workgroup_size == 0)
        return doe_vk_fail(DOE_VK_INVALID_ARGUMENT, "timestamp normalizer descriptor", error);
    if (context->lost) return doe_vk_fail(DOE_VK_DEVICE_LOST, "timestamp context", error);
    const doe_vk_caps *caps = &context->caps;
    uint64_t size = (uint64_t)descriptor->capacity * 8;
    uint32_t groups = (descriptor->capacity - 1) / descriptor->workgroup_size + 1;
    if (caps->timestamp_valid_bits == 0 || caps->timestamp_valid_bits > 64 ||
        !isfinite(caps->timestamp_period_ns) || caps->timestamp_period_ns <= 0 ||
        descriptor->workgroup_size > caps->max_workgroup_size[0] ||
        descriptor->workgroup_size > caps->max_workgroup_invocations ||
        groups > caps->max_workgroup_count[0] || size > caps->max_storage_buffer_range)
        return doe_vk_fail(DOE_VK_UNSUPPORTED, "timestamp normalization limits", error);
    if (descriptor->period_ns != caps->timestamp_period_ns ||
        descriptor->valid_bits != caps->timestamp_valid_bits)
        return doe_vk_fail(DOE_VK_INVALID_ARGUMENT, "timestamp calibration mismatch", error);
    doe_vk_timestamp_normalizer *normalizer = calloc(1, sizeof(*normalizer));
    if (normalizer == NULL) return doe_vk_fail(DOE_VK_OUT_OF_MEMORY, "timestamp normalizer owner", error);
    normalizer->references = 1;
    normalizer->capacity = descriptor->capacity;
    normalizer->workgroup_size = descriptor->workgroup_size;
    const doe_vk_binding_layout layout = {0, 0, DOE_VK_BINDING_STORAGE};
    doe_vk_status status = doe_vk_pipeline_create(context, descriptor->spirv,
        descriptor->word_count, "main", &layout, 1, &normalizer->pipeline, error);
    if (status == DOE_VK_OK) status = doe_vk_buffer_create(context, size,
        DOE_VK_BUFFER_STORAGE | DOE_VK_BUFFER_COPY_SRC | DOE_VK_BUFFER_COPY_DST,
        DOE_VK_MEMORY_DEVICE, &normalizer->scratch, error);
    if (status != DOE_VK_OK) {
        doe_vk_timestamp_normalizer_release(normalizer);
        return status;
    }
    *out = normalizer;
    return doe_vk_fail(DOE_VK_OK, "timestamp normalizer ready", error);
}

doe_vk_status doe_vk_timestamp_normalizer_retain(doe_vk_timestamp_normalizer *normalizer) {
    if (normalizer == NULL) return DOE_VK_INVALID_ARGUMENT;
    if (normalizer->references == UINT32_MAX) return DOE_VK_OUT_OF_MEMORY;
    ++normalizer->references;
    return DOE_VK_OK;
}

void doe_vk_timestamp_normalizer_release(doe_vk_timestamp_normalizer *normalizer) {
    if (normalizer == NULL || --normalizer->references != 0) return;
    doe_vk_pipeline_release(normalizer->pipeline);
    doe_vk_buffer_release(normalizer->scratch);
    free(normalizer);
}

doe_vk_status doe_vk_commands_resolve_queries_ns(doe_vk_commands *commands,
    doe_vk_query_pool *pool, uint32_t first, uint32_t count,
    doe_vk_timestamp_normalizer *normalizer, doe_vk_buffer *destination, uint64_t offset) {
    doe_vk_status status = doe_vk_recording(commands);
    if (status != DOE_VK_OK) return status;
    uint64_t size = (uint64_t)count * 8;
    if (normalizer == NULL || normalizer->pipeline->context != commands->context ||
        count > normalizer->capacity || destination == NULL ||
        destination->context != commands->context || !(destination->usage & DOE_VK_BUFFER_COPY_DST) ||
        offset % 8 != 0 || !doe_vk_range(destination->size, offset, size))
        return doe_vk_poison(commands, DOE_VK_INVALID_ARGUMENT);
    if (destination->content_status != DOE_VK_OK) return doe_vk_poison(commands, destination->content_status);
    if (count == 0) return doe_vk_commands_resolve_queries(commands, pool, first, count, destination, offset);
    status = doe_vk_commands_resolve_queries(commands, pool, first, count, normalizer->scratch, 0);
    if (status != DOE_VK_OK) return status;
    const doe_vk_buffer_binding binding = {normalizer->scratch, 0, size};
    uint32_t groups = (count - 1) / normalizer->workgroup_size + 1;
    status = doe_vk_commands_dispatch(commands, normalizer->pipeline, &binding, 1, groups, 1, 1);
    if (status != DOE_VK_OK) return status;
    return doe_vk_commands_copy(commands, normalizer->scratch, 0, destination, offset, size);
}
