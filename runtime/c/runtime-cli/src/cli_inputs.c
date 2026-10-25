/* SPDX-License-Identifier: MIT
 * Retained runtime_cli_samples.zig and runtime/trace/replay.zig contracts. */
#include "cli_inputs.h"
#include "trace.h"
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *cli_default_commands(void)
{
    return "[{\"kind\":\"copy_buffer_to_texture\",\"srcHandle\":4096,\"dstHandle\":8192,\"bytes\":4096},"
        "{\"kind\":\"upload\",\"bytes\":4096,\"alignBytes\":4},"
        "{\"kind\":\"buffer_write\",\"handle\":12288,\"bufferSize\":16,\"data\":[1,2,3,4]},"
        "{\"kind\":\"kernel_dispatch\",\"kernel\":\"bench/kernels/shader_compile_pipeline_stress.wgsl\",\"x\":2,\"y\":1,\"z\":1},"
        "{\"kind\":\"barrier\",\"dependencyCount\":3}]";
}

const char *cli_default_quirks(void)
{
    return "[{\"schemaVersion\":2,\"quirkId\":\"vulkan.intel.gen12.use_temp_buffer_compressed_copy\",\"scope\":\"memory\","
        "\"match\":{\"vendor\":\"intel\",\"deviceFamily\":\"gen12\",\"driverRange\":\">=31.0.101,<32.0.0\",\"api\":\"vulkan\"},"
        "\"action\":{\"kind\":\"use_temporary_buffer\",\"params\":{\"bufferAlignmentBytes\":4}},"
        "\"safetyClass\":\"high\",\"verificationMode\":\"lean_preferred\",\"proofLevel\":\"guarded\","
        "\"provenance\":{\"sourceRepo\":\"dawn\",\"sourcePath\":\"src/dawn/native/Toggles.cpp\",\"sourceCommit\":\"example\","
        "\"observedAt\":\"2026-02-17T00:00:00Z\"}}]";
}

static bool same_string(const doe_json_value *value, const char *text)
{
    if (!text) return !value || value->kind == DOE_JSON_NULL;
    return value && value->kind == DOE_JSON_STRING && value->string_size == strlen(text) &&
        !memcmp(value->string, text, value->string_size);
}

static bool read_hash(const doe_json_value *value, uint64_t *out)
{
    if (!value || value->kind != DOE_JSON_STRING || value->string_size != strlen(value->string) || !value->string_size)
        return false;
    const char *text = value->string;
    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) text += 2;
    if (!*text) return false;
    for (const char *c = text; *c; c++)
        if (!strchr("0123456789abcdefABCDEF", *c)) return false;
    errno = 0;
    char *end;
    unsigned long long result = strtoull(text, &end, 16);
    if (errno || *end) return false;
    *out = result;
    return true;
}

bool cli_validate_replay(const char *bytes, size_t size, const char *module, const doe_stream *stream,
    uint64_t repeats, const doe_quirk_set *quirks, char *message, size_t capacity)
{
    size_t offset = 0;
    uint64_t row = 0, previous = DOE_TRACE_HASH_SEED, total = stream->count * repeats;
    while (offset < size) {
        size_t start = offset;
        while (offset < size && bytes[offset] != '\n') offset++;
        size_t length = offset - start;
        if (offset < size) offset++;
        while (length && strchr(" \t\r", bytes[start])) { start++; length--; }
        while (length && strchr(" \t\r", bytes[start + length - 1])) length--;
        if (!length) continue;
        doe_json_document document;
        doe_json_error error;
        if (doe_json_parse(bytes + start, length, &document, &error) != DOE_JSON_OK) {
            snprintf(message, capacity, "replay row %" PRIu64 " is not JSON", row);
            return false;
        }
        const doe_json_value *v = document.root;
        const doe_json_value *module_value = doe_json_member_value(v, "module"), *opcode = doe_json_member_value(v, "opCode");
        uint64_t seq, hash, prior;
        bool ok = v->kind == DOE_JSON_OBJECT && row < total && (!module_value || same_string(module_value, module)) &&
            (!opcode || same_string(opcode, "dispatch")) && doe_json_as_u64(doe_json_member_value(v, "seq"), UINT64_MAX, &seq) &&
            seq == row && read_hash(doe_json_member_value(v, "hash"), &hash) && read_hash(doe_json_member_value(v, "previousHash"), &prior);
        if (ok) {
            const doe_command *command = &stream->commands[row % stream->count];
            doe_quirk_decision decision = doe_quirks_decide(quirks, command->kind);
            const char *kernel = command->kind == DOE_COMMAND_KERNEL_DISPATCH ? command->as.dispatch.kernel : NULL;
            uint64_t expected = doe_trace_payload_hash(previous, row, command->label, doe_command_tag(command->kind), kernel,
                &decision, &command->semantic);
            ok = same_string(doe_json_member_value(v, "command"), command->label) &&
                same_string(doe_json_member_value(v, "kernel"), kernel) && prior == previous && hash == expected;
            previous = expected;
        }
        doe_json_free(&document);
        if (!ok) {
            snprintf(message, capacity, "replay row %" PRIu64 " module/opcode/sequence/command/kernel/hash mismatch", row);
            return false;
        }
        row++;
    }
    if (row != total) {
        snprintf(message, capacity, "replay row count %" PRIu64 " does not equal command count %" PRIu64, row, total);
        return false;
    }
    return true;
}
