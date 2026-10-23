/* SPDX-License-Identifier: MIT
 * Wrong-but-typed private IR views fail before an emitter uses an identity. */
#include "../ir_view.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    IrInstruction instructions[2] = {{.result = 1u, .type = IR_U32}, {.result = 2u, .type = IR_F32}};
    FormalRecord formal = {0};
    formal.type.resolved.type = IR_I32;
    AccessStep steps[2] = {{.parent = NO_ID}, {.parent = 0u}};
    ResourceRecord resource = {.kind = DOE_COMPILER_STORAGE_BUFFER,
        .flags = DOE_COMPILER_RESOURCE_RUNTIME_TAIL, .tail_stride = 16u};
    CompilerWorkspace c = {0};
    c.instructions = instructions;
    c.limits.instructions = 2u;
    c.instruction_count = 2u;
    c.formals = &formal;
    c.formal_count = 1u;
    c.accesses = steps;
    c.access_count = 2u;
    c.resources = &resource;
    c.resource_count = 1u;
    TypeId type = IR_BOOL;
    assert(target_value_type(&c, 1u, &type) && type == IR_U32);
    assert(target_value_type(&c, 2u, &type) && type == IR_F32);
    assert(target_value_type(&c, 3u, &type) && type == IR_I32);
    assert(!target_value_type(&c, 0u, &type) && type == IR_I32);
    assert(!target_value_type(&c, 4u, &type) && type == IR_I32);
    instructions[0].result = 2u;
    assert(!target_value_type(&c, 1u, &type) && type == IR_I32);
    uint32_t path[2] = {99u, 99u}, count = 99u;
    assert(target_access_path(&c, 1u, path, 2u, &count) == NULL);
    assert(count == 2u && path[0] == 1u && path[1] == 0u);
    assert(target_access_path(&c, 1u, path, 1u, &count) != NULL);
    assert(target_access_path(&c, 2u, path, 2u, &count) != NULL);
    steps[1].parent = 1u;
    assert(target_access_path(&c, 1u, path, 2u, &count) != NULL);
    assert(target_runtime_array(&c, 0u) == &resource);
    assert(target_runtime_array(&c, 1u) == NULL);
    resource.tail_stride = 0u;
    assert(target_runtime_array(&c, 0u) == NULL);
    resource.tail_stride = 16u;
    resource.kind = DOE_COMPILER_UNIFORM_BUFFER;
    assert(target_runtime_array(&c, 0u) == NULL);
    puts("Shared target IR views: value/formal identity, bounded ordered paths, typed runtime tails passed.");
    return 0;
}
