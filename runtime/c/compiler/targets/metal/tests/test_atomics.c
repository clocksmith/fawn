/* SPDX-License-Identifier: MIT */
#include "fixture.h"
#include "../private.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void save(const char *prefix, const char *name, const char *text) {
    if (!prefix) return;
    char path[1024];
    int length = snprintf(path, sizeof(path), "%s_%s.metal", prefix, name);
    assert(length > 0 && (size_t)length < sizeof(path));
    FILE *file = fopen(path, "wb");
    assert(file && fwrite(text, 1, strlen(text), file) == strlen(text) && !fclose(file));
}
static void dense_ordering(Fixture *fixture, const char *prefix) {
    DoeCompilerMetalBindingV12 bindings[32];
    DoeCompilerMetalEntryV12 entries[8];
    DoeMetalMetadata plan = {.bindings=bindings,.binding_capacity=32,.entries=entries,.entry_capacity=8};
    MetalEmitter emitter = {.source=&fixture->source,.module=fixture->module,.selected_entry=0};
    assert(metal_dense_plan(&emitter, &plan));
    assert(plan.binding_count == 2 && plan.entry_count == 1);
    assert(entries[0].buffer_count == 2 && entries[0].sizes_slot == DOE_COMPILER_METAL_SIZES_SLOT);
    assert(entries[0].sizes_bytes == 4);
    assert(bindings[0].resource_index == 0 && bindings[0].slot == 0 && bindings[0].size_index == 0);
    assert(bindings[1].resource_index == 1 && bindings[1].slot == 1 && bindings[1].size_index == NO_ID);
    DoeMetalResult result;
    assert(doe_metal_emit_dense(&fixture->source, fixture->module, 0, fixture->scratch, &plan,
                               NULL, 0, &result) == DOE_COMPILER_OK);
    char *text = malloc(result.required_bytes + 1);
    assert(text);
    assert(doe_metal_emit_dense(&fixture->source, fixture->module, 0, fixture->scratch, &plan,
                               text, result.required_bytes, &result) == DOE_COMPILER_OK);
    text[result.written_bytes] = '\0';
    has(text, "(max((ctx.r0_bytes - min(ctx.r0_bytes, 16u)) / 4u, 1u) - 1u)");
    has(text, "r0 [[buffer(0)]]");
    assert(!strstr(text, "r0 [[buffer(18)]]"));
    uint32_t operations[3], count = 0;
    for (uint32_t i = 0; i < fixture->module->instruction_count; ++i) {
        const IrInstruction *ir = &fixture->module->instructions[i];
        if (ir->opcode == IR_ATOMIC_ADD) {
            assert(count < 3);
            operations[count++] = ir->result;
        }
    }
    assert(count == 3);
    /* Each evaluated index/RHS operation is emitted once and before the outer
     * atomic; no source replay occurs inside a guard or CAS retry. */
    const char *previous = text;
    for (unsigned i = 0; i < 3; ++i) {
        char statement[80];
        snprintf(statement, sizeof(statement), "v%u = atomic_fetch_add_explicit(target", operations[i]);
        const char *at = strstr(text, statement);
        assert(at && at > previous && !strstr(at + 1, statement));
        previous = at;
    }
    has(text, ".m1 && expected == comparison)");
    save(prefix, "dense_effects", text);
    free(text);
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
    has(text, "atomic_fetch_add_explicit(");
    has(text, "atomic_exchange_explicit(");
    has(text, "atomic_compare_exchange_weak_explicit(");
    /* Storage indices clamp (no signed guard); workgroup indices keep it. */
    assert((strstr(text, " >= 0)") != NULL) == workgroup);
    has(text, signed_value ? "atomic_int* target" : "atomic_uint* target");
    has(text, "memory_order_relaxed, memory_order_relaxed");
    has(text, ".m1 && expected == comparison)");
    if (workgroup) {
        has(text, "threadgroup DoeT");
        has(text, "doe_wzero");
        has(text, "threadgroup_barrier(mem_flags::mem_threadgroup)");
    } else {
        /* The clamped offset precedes the unconditional atomic pointer. */
        const char *clamp = strstr(text, "ulong offset = 0ul + ulong(min(uint(");
        const char *pointer = strstr(text, "reinterpret_cast<device atomic_");
        assert(clamp && pointer && clamp < pointer && !strstr(text, "offset <= bytes"));
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
        DoeMetalResult result;
        assert(doe_metal_emit(&fixture->source, fixture->module, 0, fixture->scratch,
            intact, sizeof(intact), &result) == DOE_COMPILER_UNSUPPORTED);
        assert(!strcmp(intact, "intact") && !result.required_bytes && !result.written_bytes);
        ir->operands.access.value = original;
        char *reused = emit(fixture, 0);
        assert(!strcmp(text, reused));
        free(reused);
        break;
    }
    free(text);
    release(fixture);
}
static void selected_initializers(const char *prefix) {
    Fixture *fixture = prepare("struct Row{matrix:mat3x2f,values:array<atomic<i32>,2>}"
        "var<workgroup> row:Row;var<workgroup> other:atomic<u32>;"
        "@compute @workgroup_size(2) fn main(){atomicStore(&row.values[0],-1);let n=atomicLoad(&row.values[1]);}"
        "@compute @workgroup_size(1) fn alternate(){atomicStore(&other,7u);}");
    char *text = emit(fixture, 0);
    has(text, "atomic_store_explicit(p, int(0), memory_order_relaxed)");
    has(text, "float3x2(0.0f)");
    assert(!strstr(text, "ctx.w1") && !strstr(text, "threadgroup atomic_uint* p"));
    save(prefix, "selected_shared", text);
    free(text);
    text = emit(fixture, 1);
    has(text, "atomic_store_explicit(p, uint(0), memory_order_relaxed)");
    assert(!strstr(text, "ctx.w0") && !strstr(text, "threadgroup atomic_int* p"));
    save(prefix, "alternate_shared", text);
    free(text);
    release(fixture);
}
int main(int argc, char **argv) {
    const char *prefix = argc > 1 ? argv[1] : NULL;
    for (unsigned shared = 0; shared < 2; ++shared)
        for (unsigned signed_value = 0; signed_value < 2; ++signed_value)
            all_operations(prefix, shared != 0, signed_value != 0);
    Fixture *fixture = prepare("struct S{prefix:vec4u,tail:array<atomic<i32>>}"
        "@group(1) @binding(2) var<storage,read_write> data:S;"
        "@group(0) @binding(1) var<storage,read_write> effects:atomic<u32>;"
        "@compute @workgroup_size(1) fn main(){"
        "let old=atomicAdd(&data.tail[atomicAdd(&effects,1u)],i32(atomicAdd(&effects,2u)));"
        "let result=atomicCompareExchangeWeak(&data.tail[0],old,5i);}");
    char *text = emit(fixture, 0);
    has(text, "(max((ctx.sizes[18] - min(ctx.sizes[18], 16u)) / 4u, 1u) - 1u)");
    save(prefix, "effects", text);
    free(text);
    dense_ordering(fixture, prefix);
    release(fixture);
    selected_initializers(prefix);
    puts("MSL atomics: all11 signed/unsigned storage/shared mappings, clamped storage and guarded shared pointers, ordered operands and failed publication controls passed.");
    return 0;
}
