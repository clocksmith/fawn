/* SPDX-License-Identifier: MIT
 * Numeric stability evaluation on the host: Zig's f64 JSON spelling (vectors
 * printed by Zig 0.15.2 std.fmt "{}"), the digests and layout fingerprint
 * doe-zig-runtime wrote for numeric-stability-decode-greedy on the Radeon
 * (2026-10-25), and the routes of config/numeric-stability-policy.json for a
 * fast result that misses the reference, through in-memory buffers.
 * argv[1] is the repository root, argv[2] a scratch directory for receipts. */
#include "numeric_eval.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "host_io.h"
#ifdef _WIN32
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif

static char *slurp(const char *path, size_t *size)
{
    FILE *file = fopen(path, "rb");
    assert(file);
    assert(fseek(file, 0, SEEK_END) == 0);
    long length = ftell(file);
    rewind(file);
    char *text = malloc((size_t)length + 1);
    assert(text && fread(text, 1, (size_t)length, file) == (size_t)length);
    fclose(file);
    text[length] = '\0';
    *size = (size_t)length;
    return text;
}

static void spelled(double value, const char *expected)
{
    char *bytes = NULL;
    size_t length = 0;
    doe_host_output output;
    FILE *out = doe_host_output_open(&output);
    assert(out && doe_ns_write_f64(out, value) && doe_host_output_close(&output, &bytes, &length));
    if (strcmp(bytes, expected)) fprintf(stderr, "%.17g -> %s, expected %s\n", value, bytes, expected);
    assert(!strcmp(bytes, expected));
    free(bytes);
}

/* Buffers by handle, 64 bytes each. */
typedef struct memory {
    uint64_t handles[8];
    uint8_t bytes[8][64];
    size_t count;
    unsigned writes, captures, fail_capture;
} memory;

static uint8_t *buffer(memory *m, uint64_t handle)
{
    for (size_t i = 0; i < m->count; i++) if (m->handles[i] == handle) return m->bytes[i];
    m->handles[m->count] = handle;
    return m->bytes[m->count++];
}

static bool capture(void *context, uint64_t handle, uint64_t offset, uint64_t size, uint8_t **bytes, size_t *length,
    char *message, size_t message_size)
{
    memory *m = context;
    m->captures++;
    if (m->fail_capture && m->captures == m->fail_capture) {
        snprintf(message, message_size, "injected capture failure");
        return false;
    }
    if (offset > 64 || (size != UINT64_MAX && size > 64 - offset)) {
        snprintf(message, message_size, "capture out of range");
        return false;
    }
    if (size == UINT64_MAX) size = 64 - offset;
    *bytes = malloc((size_t)size);
    memcpy(*bytes, buffer(context, handle) + offset, (size_t)size);
    *length = (size_t)size;
    return true;
}

static bool write_words(void *context, uint64_t handle, uint64_t offset, const uint32_t *words, size_t count, char *message,
    size_t message_size)
{
    (void)message, (void)message_size;
    memory *m = context;
    memcpy(buffer(m, handle) + offset, words, count * 4);
    m->writes++;
    return true;
}

static void put_f32(memory *m, uint64_t handle, const float *values, size_t count)
{
    memcpy(buffer(m, handle), values, count * sizeof(float));
}

static void put_u32(memory *m, uint64_t handle, const uint32_t *values, size_t count)
{
    memcpy(buffer(m, handle), values, count * sizeof(uint32_t));
}

static doe_kernel_binding bindings[4] = {
    {.binding = 0, .handle = 10, .buffer_type = DOE_BINDING_UNIFORM, .buffer_size = 16},
    {.binding = 1, .handle = 11, .buffer_type = DOE_BINDING_READ_ONLY_STORAGE, .buffer_size = 24},
    {.binding = 2, .handle = 12, .buffer_type = DOE_BINDING_READ_ONLY_STORAGE, .buffer_size = 12},
    {.binding = 3, .handle = 13, .buffer_type = DOE_BINDING_STORAGE, .buffer_size = 8},
};
static doe_kernel_binding sample_bindings[3] = {
    {.binding = 0, .handle = 20, .buffer_type = DOE_BINDING_UNIFORM, .buffer_size = 16},
    {.binding = 1, .handle = 13, .buffer_type = DOE_BINDING_READ_ONLY_STORAGE, .buffer_size = 8},
    {.binding = 2, .handle = 21, .buffer_type = DOE_BINDING_STORAGE, .buffer_size = 4},
};

/* Fast (GPU) logits [0, 0.5] miss the exact [1, 0.25] that the serial f32 and
 * f64 evaluations of weights [[1,0,0],[0,0.25,0]] by hidden [1,1,1] give. */
static void route(const char *root, const char *work, const char *profile_id, const char *decision, bool rewrite, bool stop, bool decode)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/config/numeric-stability-policy.json", root);
    size_t size;
    char *text = slurp(path, &size);
    doe_ns_policy policy;
    char message[256];
    assert(doe_ns_policy_load(text, size, "config/numeric-stability-policy.json", profile_id, &policy, message, sizeof(message)));
    memory m = {0};
    put_u32(&m, 10, (const uint32_t[]){2, 3, 0, 0}, 4);
    put_f32(&m, 11, (const float[]){1, 0, 0, 0, 0.25f, 0}, 6);
    put_f32(&m, 12, (const float[]){1, 1, 1}, 3);
    put_f32(&m, 13, (const float[]){0, 0.5f}, 2);
    put_u32(&m, 20, (const uint32_t[]){2, 0, 0, 0}, 4);
    put_u32(&m, 21, (const uint32_t[]){1}, 1);
    doe_command logits = {.kind = DOE_COMMAND_KERNEL_DISPATCH, .label = "kernel_dispatch"};
    logits.as.dispatch.kernel = "bench/inference-pipeline/kernels/matmul-gemv.wgsl";
    logits.as.dispatch.bindings = bindings;
    logits.as.dispatch.binding_count = 4;
    logits.semantic = (doe_semantic){.op_id = "decode.final_logits", .stage = "decode_demo", .phase = "final_logits",
        .has_token_index = true};
    doe_kernel_binding ordinary_bindings[4];
    if (!decode) {
        memcpy(ordinary_bindings, bindings, sizeof(ordinary_bindings));
        ordinary_bindings[1] = bindings[2]; ordinary_bindings[1].binding = 1;
        ordinary_bindings[2] = bindings[1]; ordinary_bindings[2].binding = 2;
        logits.as.dispatch.bindings = ordinary_bindings;
        logits.as.dispatch.kernel = "matmul_logits_forward_f16accum.wgsl";
        logits.semantic.op_id = "matmul.logits";
        logits.semantic.phase = "logits";
    }
    doe_command sample = {.kind = DOE_COMMAND_KERNEL_DISPATCH, .label = "kernel_dispatch"};
    sample.as.dispatch.kernel = "bench/inference-pipeline/kernels/sample.wgsl";
    sample.as.dispatch.bindings = sample_bindings;
    sample.as.dispatch.binding_count = 3;
    sample.semantic = (doe_semantic){.op_id = "decode.sample_token", .stage = "decode_demo", .phase = "sample_token",
        .has_token_index = true};
    char meta[1024];
    snprintf(meta, sizeof(meta), "%s/test-numeric-%d.meta.json", work, (int)getpid());
    doe_ns_recorder recorder;
    assert(doe_ns_recorder_open(&recorder, &policy, meta));
    const doe_ns_io io = {.context = &m, .capture = capture, .write = write_words};
    const doe_ns_identity identity = {.backend = "doe_c_vulkan", .vendor = "intel", .api = "vulkan", .family = "gen12",
        .driver = "31.0.101"};
    bool stopped = false;
    assert(doe_ns_record(&recorder, &io, &identity, &logits, NULL, 1, &stopped, message, sizeof(message)));
    assert(!decode || (recorder.source.fired && !strcmp(recorder.source.decision, decision)));
    assert(stopped == stop && m.writes == (rewrite ? 1u : 0u));
    float committed[2];
    memcpy(committed, buffer(&m, 13), sizeof(committed));
    assert(rewrite ? committed[0] == 1.0f && committed[1] == 0.25f : committed[0] == 0.0f);
    if (!stop && decode) {
        assert(doe_ns_record(&recorder, &io, &identity, &sample, NULL, 0, &stopped, message, sizeof(message)));
        assert(recorder.receipt_count == 2);
    }
    /* Finite inputs can overflow the stable f32 reduction. That is a failed
     * evaluation, never an argmax over Inf or a successful routing receipt. */
    put_u32(&m, 11, (const uint32_t[]){0x7f7fffff, 0x7f7fffff, 0x7f7fffff}, 3);
    put_f32(&m, 12, (const float[]){2, 2, 2}, 3);
    doe_ns_evaluation overflow = {.command = &logits};
    message[0] = 0;
    assert(!doe_ns_evaluate_matmul(&io, &overflow, decode, message, sizeof(message)));
    assert(strstr(message, "nonfinite evaluation"));
    free(overflow.candidates);
    assert(recorder.rewrite_count == (rewrite ? 1u : 0u) && recorder.stop_count == (stop ? 1u : 0u));
    char *receipt_path = doe_host_strdup(recorder.receipt_path);
    doe_ns_recorder_free(&recorder);
    char *receipts = slurp(receipt_path, &size);
    assert(strstr(receipts, "\"fired\":true") && strstr(receipts, decision));
    assert(strstr(receipts, "\"selectedToken\":{\"fast\":1,\"stable\":0,\"reference\":0,\"fastMatchesReference\":false"));
    remove(receipt_path);
    free(receipt_path);
    free(receipts);
    doe_ns_policy_free(&policy);
    free(text);
}

/* Uniform zero scores yield the exact mean of the value rows. Deliberately
 * reversed fast output exercises the policy, actual target rewrite and receipt. */
static void attention_route(const char *root, const char *work)
{
    char path[1024], message[256] = "";
    snprintf(path, sizeof(path), "%s/config/numeric-stability-policy.json", root);
    size_t size;
    char *text = slurp(path, &size);
    doe_ns_policy policy;
    assert(doe_ns_policy_load(text, size, path, NULL, &policy, message, sizeof(message)));
    memory m = {0};
    put_u32(&m, 10, (const uint32_t[]){2, 2, 0x3f800000}, 3);
    put_f32(&m, 11, (const float[]){0, 0}, 2);
    put_f32(&m, 12, (const float[]){1, 2, 3, 4}, 4);
    put_f32(&m, 13, (const float[]){2, 0, 0.5f, 0}, 4);
    put_f32(&m, 14, (const float[]){0, 2}, 2);
    doe_kernel_binding captures[5] = {
        {.binding = 0, .handle = 10, .buffer_type = DOE_BINDING_UNIFORM, .buffer_size = 12},
        {.binding = 1, .handle = 11, .buffer_type = DOE_BINDING_READ_ONLY_STORAGE, .buffer_size = 8},
        {.binding = 2, .handle = 12, .buffer_type = DOE_BINDING_READ_ONLY_STORAGE, .buffer_size = 16},
        {.binding = 3, .handle = 13, .buffer_type = DOE_BINDING_READ_ONLY_STORAGE, .buffer_size = 16},
        {.binding = 4, .handle = 14, .buffer_type = DOE_BINDING_STORAGE, .buffer_size = 8},
    };
    doe_command command = {.kind = DOE_COMMAND_KERNEL_DISPATCH, .label = "kernel_dispatch"};
    command.as.dispatch.kernel = "attention_slice_forward_v2_f32.wgsl";
    command.as.dispatch.bindings = captures;
    command.as.dispatch.binding_count = 5;
    command.semantic = (doe_semantic){.op_id = "attention.output", .stage = "prefill", .phase = "output_logits"};
    const doe_ns_io io = {.context = &m, .capture = capture, .write = write_words};
    const doe_ns_identity identity = {.backend = "host-test"};
    snprintf(path, sizeof(path), "%s/test-attention-%d.meta.json", work, (int)getpid());
    doe_ns_recorder recorder;
    assert(doe_ns_recorder_open(&recorder, &policy, path));
    bool stop;
    assert(doe_ns_record(&recorder, &io, &identity, &command, NULL, 1, &stop, message, sizeof(message)));
    assert(!stop && recorder.receipt_count == 1 && recorder.rewrite_count == 1 && m.writes == 1);
    float output[2];
    memcpy(output, buffer(&m, 14), sizeof(output));
    assert(output[0] == 1.25f && output[1] == 0);
    char *receipt_path = doe_host_strdup(recorder.receipt_path);
    doe_ns_recorder_free(&recorder);
    char *receipt = slurp(receipt_path, &size);
    assert(strstr(receipt, "attention-prefill-slice") && strstr(receipt, "prefer-stable"));
    free(receipt);
    remove(receipt_path);
    free(receipt_path);
    assert(doe_ns_recorder_open(&recorder, &policy, path));
    m.fail_capture = m.captures + 3;
    assert(!doe_ns_record(&recorder, &io, &identity, &command, NULL, 0, &stop, message, sizeof(message)));
    assert(!strcmp(message, "injected capture failure") && recorder.receipt_count == 0 && m.writes == 1);
    doe_ns_recorder_free(&recorder);
    m.fail_capture = 0;
    captures[4].buffer_size = 12;
    doe_ns_evaluation evaluation = {.command = &command};
    assert(!doe_ns_evaluate_attention(&io, &evaluation, message, sizeof(message)));
    assert(strstr(message, "invalid attention shape") && !evaluation.candidates && m.writes == 1);
    captures[4].buffer_size = 8;
    captures[1].resource = DOE_RESOURCE_TEXTURE;
    assert(!doe_ns_evaluate_attention(&io, &evaluation, message, sizeof(message)));
    assert(!evaluation.candidates && m.writes == 1);
    doe_ns_policy_free(&policy);
    free(text);
}

/* Four output elements feed two decision rows: rewriting candidate logits
 * instead of the RMSNorm tensor would corrupt this unchanged downstream input. */
static void rmsnorm_route(const char *root, const char *work)
{
    char path[1024], message[256] = "";
    snprintf(path, sizeof(path), "%s/config/numeric-stability-policy.json", root);
    size_t size;
    char *text = slurp(path, &size);
    doe_ns_policy policy;
    assert(doe_ns_policy_load(text, size, path, NULL, &policy, message, sizeof(message)));
    memory m = {0};
    put_u32(&m, 10, (const uint32_t[]){4, 0}, 2);
    put_f32(&m, 11, (const float[]){1, 1, 1, 1}, 4);
    put_f32(&m, 12, (const float[]){2, 1, 1, 1}, 4);
    put_f32(&m, 13, (const float[]){0, 2, 0, 0}, 4);
    put_u32(&m, 20, (const uint32_t[]){2, 4}, 2);
    put_f32(&m, 21, (const float[]){1, 0, 0, 0, 0, 1, 0, 0}, 8);
    doe_kernel_binding captures[4] = {
        {.binding = 0, .handle = 10, .buffer_type = DOE_BINDING_UNIFORM, .buffer_size = 8},
        {.binding = 1, .handle = 11, .buffer_type = DOE_BINDING_READ_ONLY_STORAGE, .buffer_size = 16},
        {.binding = 2, .handle = 12, .buffer_type = DOE_BINDING_READ_ONLY_STORAGE, .buffer_size = 16},
        {.binding = 3, .handle = 13, .buffer_type = DOE_BINDING_STORAGE, .buffer_size = 16},
    };
    doe_kernel_binding downstream_bindings[3] = {
        {.binding = 0, .handle = 20, .buffer_type = DOE_BINDING_UNIFORM, .buffer_size = 8},
        {.binding = 2, .handle = 21, .buffer_type = DOE_BINDING_READ_ONLY_STORAGE, .buffer_size = 32},
        {.binding = 1, .handle = 13, .buffer_type = DOE_BINDING_READ_ONLY_STORAGE, .buffer_size = 16},
    };
    doe_command command = {.kind = DOE_COMMAND_KERNEL_DISPATCH, .label = "kernel_dispatch"};
    command.as.dispatch.kernel = "rmsnorm.wgsl";
    command.as.dispatch.bindings = captures;
    command.as.dispatch.binding_count = 4;
    command.semantic = (doe_semantic){.op_id = "rmsnorm.output", .stage = "prefill", .phase = "normalized_hidden"};
    doe_command next = {.kind = DOE_COMMAND_KERNEL_DISPATCH};
    next.as.dispatch.kernel = "matmul_logits_forward_f16accum.wgsl";
    next.as.dispatch.bindings = downstream_bindings;
    next.as.dispatch.binding_count = 3;
    next.semantic.op_id = "matmul.logits";
    const doe_ns_io io = {.context = &m, .capture = capture, .write = write_words};
    const doe_ns_identity identity = {.backend = "host-test"};
    snprintf(path, sizeof(path), "%s/test-rmsnorm-%d.meta.json", work, (int)getpid());
    doe_ns_recorder recorder;
    assert(doe_ns_recorder_open(&recorder, &policy, path));
    bool stop;
    assert(doe_ns_record(&recorder, &io, &identity, &command, &next, 1, &stop, message, sizeof(message)));
    assert(!stop && recorder.receipt_count == 1 && recorder.rewrite_count == 1 && m.writes == 1);
    float output[4];
    memcpy(output, buffer(&m, 13), sizeof(output));
    assert(output[0] == 2 && output[1] == 1 && output[2] == 1 && output[3] == 1);
    char *receipt_path = doe_host_strdup(recorder.receipt_path);
    doe_ns_recorder_free(&recorder);
    remove(receipt_path);
    free(receipt_path);
    doe_ns_evaluation evaluation = {.command = &command};
    assert(!doe_ns_evaluate_rmsnorm(&io, &evaluation, NULL, 0, message, sizeof(message)));
    assert(strstr(message, "missing downstream") && !evaluation.candidates);
    downstream_bindings[2].handle = 11;
    assert(!doe_ns_evaluate_rmsnorm(&io, &evaluation, &next, 1, message, sizeof(message)));
    assert(strstr(message, "identity mismatch") && !evaluation.candidates);
    downstream_bindings[2].handle = 13;
    captures[1].buffer_size = 12;
    assert(!doe_ns_evaluate_rmsnorm(&io, &evaluation, &next, 1, message, sizeof(message)));
    assert(strstr(message, "invalid RMSNorm shape") && !evaluation.candidates);
    captures[1].buffer_size = 16;
    downstream_bindings[1].buffer_size = 28;
    assert(!doe_ns_evaluate_rmsnorm(&io, &evaluation, &next, 1, message, sizeof(message)));
    assert(strstr(message, "downstream shape") && !evaluation.candidates);
    downstream_bindings[1].buffer_size = 32;
    doe_command intervening[2] = {command, next};
    assert(!doe_ns_evaluate_rmsnorm(&io, &evaluation, intervening, 2, message, sizeof(message)));
    assert(strstr(message, "ambiguous") && !evaluation.candidates);
    put_u32(&m, 10, (const uint32_t[]){4, 0x7fc00000}, 2);
    assert(!doe_ns_evaluate_rmsnorm(&io, &evaluation, &next, 1, message, sizeof(message)));
    assert(strstr(message, "invalid RMSNorm shape") && !evaluation.candidates);
    put_u32(&m, 11, (const uint32_t[]){0x7f800000}, 1);
    double captured;
    assert(!doe_ns_capture_f32(&io, 11, 0, 1, &captured, message, sizeof(message)));
    assert(strstr(message, "nonfinite capture") && m.writes == 1);
    put_u32(&m, 10, (const uint32_t[]){3, 0}, 2);
    assert(!doe_ns_evaluate_rmsnorm(&io, &evaluation, &next, 1, message, sizeof(message)));
    assert(strstr(message, "invalid RMSNorm shape") && !evaluation.candidates);
    doe_ns_policy_free(&policy);
    free(text);
}

static void annotated_route(const char *root, const char *work)
{
    const char *json = "[{\"kind\":\"kernel_dispatch\",\"kernel\":\"arbitrary.wgsl\","
        "\"semanticOpId\":\"matmul.logits\",\"semanticStage\":\"decode\",\"semanticPhase\":\"logits\","
        "\"numericStability\":{\"hiddenState\":{\"bufferHandle\":12,\"elementCount\":3},"
        "\"logits\":{\"bufferHandle\":13,\"elementCount\":2},"
        "\"weights\":{\"bufferHandle\":11,\"rowStrideElements\":3},"
        "\"candidates\":[{\"tokenId\":7,\"rowIndex\":1,\"bias\":2,\"label\":\"selected\"},"
        "{\"token_id\":4,\"row_index\":0}]}}]";
    doe_stream stream;
    doe_stream_error error;
    assert(doe_stream_parse(json, strlen(json), &stream, &error));
    assert(stream.commands[0].numeric && stream.commands[0].numeric->candidate_count == 2);
    doe_host_output roundtrip_output;
    FILE *roundtrip_file = doe_host_output_open(&roundtrip_output);
    assert(roundtrip_file && fputs("[{\"kind\":\"kernel_dispatch\",\"kernel\":\"arbitrary.wgsl\"", roundtrip_file) >= 0);
    assert(doe_ns_write_annotation(roundtrip_file, stream.commands[0].numeric));
    assert(fputs("}]", roundtrip_file) >= 0);
    char *roundtrip;
    size_t roundtrip_size;
    assert(doe_host_output_close(&roundtrip_output, &roundtrip, &roundtrip_size));
    doe_stream parsed;
    assert(doe_stream_parse(roundtrip, roundtrip_size, &parsed, &error));
    assert(parsed.commands[0].numeric->candidates[0].token_id == 7 &&
        parsed.commands[0].numeric->candidates[0].bias == 2 &&
        !strcmp(parsed.commands[0].numeric->candidates[0].label, "selected"));
    doe_stream_free(&parsed);
    free(roundtrip);
    char path[1024], message[256] = "";
    snprintf(path, sizeof(path), "%s/config/numeric-stability-policy.json", root);
    size_t size;
    char *text = slurp(path, &size);
    doe_ns_policy policy;
    /* Explicit routing overrides the observe-only auto-detection profile. */
    assert(doe_ns_policy_load(text, size, path, "numeric-stability/observe-only-ordinary-execution-v1",
        &policy, message, sizeof(message)));
    memory m = {0};
    put_f32(&m, 11, (const float[]){1, 0, 0, 0, 0.25f, 0}, 6);
    put_f32(&m, 12, (const float[]){1, 1, 1}, 3);
    put_f32(&m, 13, (const float[]){0, 2}, 2);
    const doe_ns_io io = {.context = &m, .capture = capture, .write = write_words};
    const doe_ns_identity identity = {.backend = "host-test"};
    snprintf(path, sizeof(path), "%s/test-annotation-%d.meta.json", work, (int)getpid());
    doe_ns_recorder recorder;
    assert(doe_ns_recorder_open(&recorder, &policy, path));
    bool stop;
    assert(doe_ns_record(&recorder, &io, &identity, &stream.commands[0], NULL, 0, &stop, message, sizeof(message)));
    assert(!stop && recorder.annotation_count == 1 && recorder.auto_detect_count == 0 && recorder.rewrite_count == 1);
    float output[2];
    memcpy(output, buffer(&m, 13), sizeof(output));
    assert(output[0] == 2.25f && output[1] == 1);
    char *receipt_path = doe_host_strdup(recorder.receipt_path);
    doe_ns_recorder_free(&recorder);
    char *receipt = slurp(receipt_path, &size);
    assert(strstr(receipt, "\"tokenId\":7,\"label\":\"selected\"") && strstr(receipt, "prefer-stable"));
    free(receipt);
    remove(receipt_path);
    free(receipt_path);
    doe_ns_policy_free(&policy);
    free(text);
    doe_stream_free(&stream);
    const char *bad = "[{\"kind\":\"kernel_dispatch\",\"kernel\":\"x\",\"numericStability\":{}}]";
    assert(!doe_stream_parse(bad, strlen(bad), &stream, &error));
    assert(!stream.commands && !stream.count);
}


#include "numeric_parity_cases.h"

int main(int argc, char **argv)
{
    assert(argc == 3);
    bias_route(argv[1], argv[2]);
    policy_types(argv[1]);
    annotated_route(argv[1], argv[2]);
    rmsnorm_route(argv[1], argv[2]);
    attention_route(argv[1], argv[2]);
    spelled(1, "1");
    spelled(0.5, "0.5");
    spelled(0, "0");
    spelled(-0.0, "-0");
    spelled(1e8, "100000000");
    spelled(1e-5, "0.00001");
    spelled(123456789012345678901234.0, "123456789012345690000000");
    spelled(0.1, "0.1");
    spelled(1.0 / 3.0, "0.3333333333333333");
    spelled(1e21, "1000000000000000000000");
    spelled(1e-7, "0.0000001");
    spelled(3.4028234663852886e38, "340282346638528860000000000000000000000");
    spelled(-2.5, "-2.5");
    spelled((double)0.1f, "0.10000000149011612");
    spelled(1.5e16, "15000000000000000");
    spelled(123.456e-10, "0.0000000123456");
    spelled(0.24491866240370913, "0.24491866240370913");
    char digest[65];
    assert(doe_ns_digest((const double[]){1, 0.5}, 2, digest));
    assert(!strcmp(digest, "4153d42c8b6cac14a35cd168c8dba27c4cf3c9d1672d385ad7e4a8d95647c280"));
    assert(doe_ns_digest((const double[]){0, 0.5}, 2, digest));
    assert(!strcmp(digest, "fe2d1b548ba10cc73a8692776fe74c6689bd4561e6fc596d3bf0c970df16f308"));
    /* The greedy stream's matmul-gemv bindings: uniform 16, readonly 24 and 12, storage 8. */
    doe_command greedy = {.kind = DOE_COMMAND_KERNEL_DISPATCH};
    greedy.as.dispatch.bindings = bindings;
    greedy.as.dispatch.binding_count = 4;
    assert(doe_ns_layout_fingerprint(&greedy, digest));
    assert(!strcmp(digest, "e4d7cc2e5ddc5ba5494a469f66c0a853b78a7b2ba5bdaa862de16fd41d184644"));
    for (unsigned decode = 0; decode < 2; decode++) {
        route(argv[1], argv[2], NULL, "prefer-stable", true, false, decode != 0);
        route(argv[1], argv[2], "numeric-stability/cautious-ordinary-execution-v1", "abstain", false, true, decode != 0);
        route(argv[1], argv[2], "numeric-stability/observe-only-ordinary-execution-v1", "accept-fast", false, false, decode != 0);
    }
    puts("runtime-cli numeric: Zig f64 spelling, Radeon digests and layout fingerprint, and prefer-stable rewrite, "
        "abstain stop and accept-fast routes (host only)");
    return 0;
}
