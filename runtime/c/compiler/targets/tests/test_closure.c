/* SPDX-License-Identifier: MIT */
#include "../closure.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    IrInstruction ir[2] = {{.opcode = IR_CALL}, {.opcode = IR_ACCESS_STORE, .type = 7u}};
    ir[0].operands.scalar.binding = 1u;
    ir[1].operands.access.category = ROOT_RESOURCE;
    ir[1].operands.access.root = 0u;
    FunctionRecord functions[3] = {{.instruction_start = 0u, .instruction_end = 1u},
        {.instruction_start = 1u, .instruction_end = 2u}, {.instruction_start = 2u, .instruction_end = 2u}};
    EntryRecord entry = {.function = 0u};
    TypeRecord types[2] = {{.kind = TYPE_ARRAY, .element = 8u, .count = 2u},
        {.kind = TYPE_STRUCT, .element = 0u, .count = 2u}};
    MemberRecord members[2] = {{.type = IR_U32}, {.type = IR_F32}};
    CompilerWorkspace c = {.instructions = ir, .instruction_count = 2u,
        .functions = functions, .function_count = 3u, .entries = &entry, .entry_count = 1u,
        .types = types, .type_count = 2u, .members = members, .member_count = 2u};
    uint64_t work = 10000u;
    const char *error = NULL;
    assert(target_function_reachable(&c, 0u, 1u, &work, &error) && error == NULL);
    assert(!target_function_reachable(&c, 0u, 2u, &work, &error) && error == NULL);
    assert(target_access_helper_used(&c, 0u, ROOT_RESOURCE, 0u, IR_F32, true, &work, &error));
    assert(!target_access_helper_used(&c, 0u, ROOT_RESOURCE, 0u, IR_F32, false, &work, &error));
    assert(!target_access_helper_used(&c, 0u, ROOT_RESOURCE, 1u, IR_F32, true, &work, &error));
    assert(!target_memory_dependency(&c, 7u, IR_I32, &work, &error) && error == NULL);
    types[1].kind = TYPE_VECTOR;
    types[1].element = IR_F32;
    types[1].count = 4u;
    assert(!target_memory_dependency(&c, 7u, IR_F32, &work, &error) && error == NULL);
    assert(target_memory_dependency(&c, 7u, 8u, &work, &error));
    types[1].kind = TYPE_ARRAY;
    types[1].element = 7u;
    assert(!target_memory_dependency(&c, 7u, IR_F32, &work, &error) && error != NULL);
    error = NULL;
    ir[0].operands.scalar.binding = 0u;
    assert(!target_function_reachable(&c, 0u, 2u, &work, &error) && error != NULL);
    error = NULL;
    ir[0].operands.scalar.binding = 99u;
    assert(!target_function_reachable(&c, 0u, 2u, &work, &error) && error != NULL);
    error = NULL;
    work = 0u;
    assert(!target_memory_dependency(&c, IR_U32, IR_U32, &work, &error) && error != NULL);
    puts("Shared target closure: transitive calls, exact root/mode, aggregate dependencies, cycles and work bounds passed.");
    return 0;
}
