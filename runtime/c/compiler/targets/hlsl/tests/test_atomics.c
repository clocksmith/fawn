/* SPDX-License-Identifier: MIT */
#include "test_support.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void save(const char *prefix, const char *name, const char *text) {
    if (!prefix) return;
    char path[1024];
    int length = snprintf(path, sizeof(path), "%s_%s.hlsl", prefix, name);
    assert(length > 0 && (size_t)length < sizeof(path));
    FILE *file = fopen(path, "wb");
    assert(file && fwrite(text, 1, strlen(text), file) == strlen(text) && !fclose(file));
}
static void all_operations(const char *prefix, bool workgroup, bool signed_value) {
    const char *scalar = signed_value ? "i32" : "u32";
    char source[4096];
    int length = snprintf(source, sizeof(source),
        "@group(0) @binding(0) var<storage,read_write> a:array<atomic<%s>>;"
        "@group(0) @binding(1) var<storage,read_write> out:array<%s>;"
        "struct Row{prefix:u32,values:array<atomic<%s>,2>}var<workgroup> row:Row;"
        "@compute @workgroup_size(1) fn main(){let index=i32(out[15]);"
        "out[0]=atomicLoad(&%s[index]);atomicStore(&%s[index],%s);"
        "out[1]=atomicAdd(&%s[index],3);out[2]=atomicSub(&%s[index],2);"
        "out[3]=atomicMax(&%s[index],5);out[4]=atomicMin(&%s[index],%s);"
        "out[5]=atomicAnd(&%s[index],3);out[6]=atomicOr(&%s[index],4);"
        "out[7]=atomicXor(&%s[index],6);out[8]=atomicExchange(&%s[index],9);"
        "let success=atomicCompareExchangeWeak(&%s[index],9,11);"
        "out[9]=success.old_value;out[10]=%s(success.exchanged);"
        "let failure=atomicCompareExchangeWeak(&%s[index],0,17);"
        "out[11]=failure.old_value;out[12]=%s(failure.exchanged);out[13]=atomicLoad(&%s[index]);}",
        scalar, scalar, scalar, workgroup ? "row.values" : "a", workgroup ? "row.values" : "a",
        signed_value ? "-7" : "4294967293u", workgroup ? "row.values" : "a", workgroup ? "row.values" : "a",
        workgroup ? "row.values" : "a", workgroup ? "row.values" : "a", signed_value ? "-5" : "9",
        workgroup ? "row.values" : "a", workgroup ? "row.values" : "a", workgroup ? "row.values" : "a",
        workgroup ? "row.values" : "a", workgroup ? "row.values" : "a", scalar,
        workgroup ? "row.values" : "a", scalar, workgroup ? "row.values" : "a");
    assert(length > 0 && (size_t)length < sizeof(source));
    Fixture *fixture = prepare(source);
    char *text = emit(fixture, 0);
    has(text, "InterlockedAdd(");
    has(text, "InterlockedExchange(");
    has(text, "InterlockedCompareExchange(");
    if (workgroup) has(text, " >= 0)"); /* Workgroup memory keeps its guard. */
    if (signed_value) {
        has(text, "asint(seen) < asint(");
        has(text, "asint(seen) > asint(");
        assert(!strstr(text, "InterlockedMin(") && !strstr(text, "InterlockedMax("));
    } else {
        has(text, "InterlockedMin(");
        has(text, "InterlockedMax(");
    }
    if (workgroup) {
        has(text, "groupshared DoeT");
        has(text, "GroupMemoryBarrierWithGroupSync()");
    } else {
        /* Runtime-array atomics clamp the index to the last bound word; the
         * interlocked operation is unconditional. */
        has(text, "min(uint(");
        has(text, "(doe_r0_bytes() >= 4u ? doe_r0_bytes() / 4u - 1u : 0u)");
        assert(!strstr(text, "- offset") && !strstr(text, "if ((uint(") && !strstr(text, " >= 0)"));
    }
    save(prefix, workgroup ? signed_value ? "signed_shared" : "unsigned_shared" :
        signed_value ? "signed_storage" : "unsigned_storage", text);
    /* A malformed typed operand span fails measurement, preserving every byte. */
    for (uint32_t i = 0; i < fixture->module->instruction_count; ++i) {
        IrInstruction *ir = &fixture->module->instructions[i];
        if (ir->opcode != IR_ATOMIC_ADD) continue;
        const uint32_t original = ir->operands.access.value;
        ir->operands.access.value = fixture->module->operand_count;
        char intact[32] = "intact";
        DoeHlslResult result;
        assert(doe_hlsl_emit(&fixture->source, fixture->module, 0, fixture->scratch,
            intact, sizeof(intact), &result) == DOE_COMPILER_UNSUPPORTED);
        assert(!strcmp(intact, "intact") && !result.required_bytes && !result.written_bytes);
        ir->operands.access.value = original;
        break;
    }
    free(text);
    release(fixture);
}
void test_atomics(const char *prefix) {
    for (unsigned shared = 0; shared < 2; ++shared)
        for (unsigned signed_value = 0; signed_value < 2; ++signed_value)
            all_operations(prefix, shared != 0, signed_value != 0);
    Fixture *fixture = prepare("struct S{prefix:vec4u,tail:array<atomic<i32>>}"
        "@group(0) @binding(0) var<storage,read_write> data:S;"
        "@group(0) @binding(1) var<storage,read_write> effects:atomic<u32>;"
        "@compute @workgroup_size(1) fn main(){"
        "let old=atomicAdd(&data.tail[atomicAdd(&effects,1u)],i32(atomicAdd(&effects,2u)));"
        "let result=atomicCompareExchangeWeak(&data.tail[0],old,5i);}");
    char *text = emit(fixture, 0);
    has(text, "(doe_r0_bytes() >= 20u ? (doe_r0_bytes() - 16u) / 4u - 1u : 0u)");
    /* Clamped offsets need no guard; sizes are table reads, not context fields. */
    assert(!strstr(text, "if (") && !strstr(text, "ctx.doe_bytes"));
    has(text, "uint doe_r0_bytes() { return doe_storage_sizes[0u][0u]; }");
    assert(!strstr(text, "doe_r1_bytes")); /* The fixed atomic needs no size. */
    save(prefix, "effects", text);
    free(text);
    release(fixture);
    puts("HLSL atomics: all11 signed/unsigned storage/shared mappings, clamped storage/guarded shared bounds, compare-exchange results and failed publication controls passed.");
}
