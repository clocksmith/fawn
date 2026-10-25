/* SPDX-License-Identifier: MIT */
#include "../closure.h"
#include <assert.h>
#include <stdio.h>

static void shared_dags(void) {
    FunctionRecord functions[27] = {0};
    IrInstruction ir[50] = {0};
    EntryRecord entry = {.function = 25u};
    TypeRecord types[25] = {0};
    MemberRecord members[50] = {0};
    for (uint32_t i = 1u; i <= 25u; ++i) {
        functions[i].instruction_start = 2u * (i - 1u);
        functions[i].instruction_end = 2u * i;
        ir[2u * (i - 1u)].opcode = IR_CALL;
        ir[2u * (i - 1u) + 1u].opcode = IR_CALL;
        ir[2u * (i - 1u)].operands.scalar.binding = i - 1u;
        ir[2u * (i - 1u) + 1u].operands.scalar.binding = i - 1u;
        types[i - 1u] = (TypeRecord){.kind = TYPE_STRUCT, .element = 2u * (i - 1u), .count = 2u};
        const TypeId child = i == 1u ? IR_U32 : i + IR_PRIMITIVE_COUNT - 2u;
        members[2u * (i - 1u)].type = child;
        members[2u * (i - 1u) + 1u].type = child;
    }
    CompilerWorkspace c = {.functions = functions, .function_count = 27u,
        .instructions = ir, .instruction_count = 50u, .entries = &entry, .entry_count = 1u,
        .types = types, .type_count = 25u, .members = members, .member_count = 50u};
    unsigned char bytes[27u + 25u + IR_PRIMITIVE_COUNT];
    TargetClosure closure;
    assert(target_closure_bind(&c, (DoeTargetScratch){bytes, sizeof(bytes)}, &closure));
    uint64_t work = 1000u;
    const char *error = NULL;
    assert(target_closure_build(&c, 0u, &closure, &work, &error) && error == NULL);
    assert(work >= 800u && target_function_used(&closure, 0u));
    assert(!target_function_used(&closure, 26u));
    assert(!target_memory_dependency(&c, &closure, 24u + IR_PRIMITIVE_COUNT, IR_F32, &work, &error) && error == NULL);
    assert(work >= 600u && target_function_used(&closure, 0u));
    assert(target_type_order_begin(&closure, &work, &error));
    unsigned type_depth;
    assert(target_type_depth(&c, &closure, 24u + IR_PRIMITIVE_COUNT, &type_depth, &work, &error));
    assert(type_depth == 25u && work >= 400u);
    const uint64_t before_cached = work;
    assert(target_type_depth(&c, &closure, 24u + IR_PRIMITIVE_COUNT, &type_depth, &work, &error));
    assert(work == before_cached && target_function_used(&closure, 0u));
    /* Retained function closure survives a separate completed-type walk. */
    ir[0].operands.scalar.binding = 25u;
    error = NULL;
    assert(!target_closure_build(&c, 0u, &closure, &work, &error) && error != NULL);
    ir[0].operands.scalar.binding = 0u;
    error = NULL;
    assert(target_closure_build(&c, 0u, &closure, &work, &error));
    work = 0u;
    assert(!target_closure_build(&c, 0u, &closure, &work, &error));
}
int main(void) {
    shared_dags();
    IrInstruction ir[2] = {{.opcode = IR_CALL}, {.opcode = IR_ACCESS_STORE, .type = IR_PRIMITIVE_COUNT}};
    ir[0].operands.scalar.binding = 1u;
    ir[1].operands.access.category = ROOT_RESOURCE;
    ir[1].operands.access.root = 0u;
    FunctionRecord functions[3] = {{.instruction_start = 0u, .instruction_end = 1u},
        {.instruction_start = 1u, .instruction_end = 2u}, {.instruction_start = 2u, .instruction_end = 2u}};
    EntryRecord entry = {.function = 0u};
    TypeRecord types[2] = {{.kind = TYPE_ARRAY, .element = IR_PRIMITIVE_COUNT + 1u, .count = 2u},
        {.kind = TYPE_STRUCT, .element = 0u, .count = 2u}};
    MemberRecord members[2] = {{.type = IR_U32}, {.type = IR_F32}};
    CompilerWorkspace c = {.instructions = ir, .instruction_count = 2u,
        .functions = functions, .function_count = 3u, .entries = &entry, .entry_count = 1u,
        .types = types, .type_count = 2u, .members = members, .member_count = 2u};
    unsigned char scratch[32];
    TargetClosure closure;
    assert(!target_closure_bind(&c, (DoeTargetScratch){scratch, 1u}, &closure));
    assert(target_closure_bind(&c, (DoeTargetScratch){scratch, sizeof(scratch)}, &closure));
    uint64_t work = 10000u;
    const char *error = NULL;
    assert(target_closure_build(&c, 0u, &closure, &work, &error));
    assert(target_function_used(&closure, 1u));
    assert(!target_function_used(&closure, 2u));
    assert(target_access_helper_used(&c, &closure, ROOT_RESOURCE, 0u, IR_F32, true, &work, &error));
    assert(!target_access_helper_used(&c, &closure, ROOT_RESOURCE, 0u, IR_F32, false, &work, &error));
    assert(!target_access_helper_used(&c, &closure, ROOT_RESOURCE, 1u, IR_F32, true, &work, &error));
    assert(!target_memory_dependency(&c, &closure, IR_PRIMITIVE_COUNT, IR_I32, &work, &error) && error == NULL);
    types[1].kind = TYPE_VECTOR;
    types[1].element = IR_F32;
    types[1].count = 4u;
    assert(!target_memory_dependency(&c, &closure, IR_PRIMITIVE_COUNT, IR_F32, &work, &error) && error == NULL);
    assert(target_memory_dependency(&c, &closure, IR_PRIMITIVE_COUNT, IR_PRIMITIVE_COUNT + 1u, &work, &error));
    types[1].kind = TYPE_ARRAY;
    types[1].element = IR_PRIMITIVE_COUNT;
    assert(!target_memory_dependency(&c, &closure, IR_PRIMITIVE_COUNT, IR_F32, &work, &error) && error != NULL);
    error = NULL;
    ir[0].operands.scalar.binding = 0u;
    assert(!target_closure_build(&c, 0u, &closure, &work, &error) && error != NULL);
    error = NULL;
    ir[0].operands.scalar.binding = 99u;
    assert(!target_closure_build(&c, 0u, &closure, &work, &error) && error != NULL);
    error = NULL;
    work = 0u;
    assert(!target_memory_dependency(&c, &closure, IR_U32, IR_U32, &work, &error) && error != NULL);
    puts("Shared target closure: transitive calls, exact root/mode, aggregate dependencies, cycles and work bounds passed.");
    return 0;
}
