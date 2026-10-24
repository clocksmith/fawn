/* SPDX-License-Identifier: MIT
 * Compiler-produced HLSL/metadata control; no adapter, SDK or GPU execution. */
#include "../src/d3d12_indirect_layout.h"
#include "../../common/include/doe_dispatch_guard.h"
#include <assert.h>
#include <stdalign.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static void *allocate(void *userdata, size_t size, size_t alignment)
{
    (void)userdata;
    assert(alignment <= alignof(max_align_t));
    return malloc(size);
}
static void deallocate(void *userdata, void *memory, size_t size, size_t alignment)
{
    (void)userdata; (void)size; (void)alignment;
    free(memory);
}
int main(void)
{
    const uint32_t limits[3] = {7,11,13};
    char source[DOE_DISPATCH_GUARD_SOURCE_CAPACITY];
    size_t size = 0;
    assert(doe_dispatch_guard_source(limits,source,sizeof(source),&size) == DOE_DISPATCH_GUARD_SOURCE_OK);
    /* Standalone conservative semantic policy: this guard uses no uniform
     * layout extension or extended binding number. Native policy stays owned
     * by native_shader_semantic_flags, not duplicated in this control. */
    const doe_shader_options options = {.target=DOE_SHADER_HLSL,
        .allocator={NULL,allocate,deallocate},.capacities={.typed={
            .source_bytes=4096,.tokens=1024,.statements=128,.expressions=1024,
            .types=64,.members=64,.resources=8,.access_steps=512,.constants=128,
            .constant_edges=256,.ir_instructions=4096,.ir_operands=4096,
            .target_declarations=1024,.traversal_items=97,.words=16384,
            .expression_depth=32,.control_depth=16,.type_syntax_depth=32},
            .entries=4,.entry_inputs=16,.entry_references=16},
        .selection={.mode=DOE_COMPILER_SELECT_NONE},
        .compiler_identity="indirect-guard-control",.compiler_identity_size=22};
    doe_shader *shader = NULL;
    doe_shader_error error = {0};
    doe_shader_status status = doe_shader_create(&options,source,size,&shader,&error);
    if (status != DOE_SHADER_OK) fprintf(stderr,"guard compile: %d %s\n",(int)status,error.diagnostic.message);
    assert(status == DOE_SHADER_OK);
    doe_shader_view view;
    assert(doe_shader_borrow(shader,&view) == DOE_SHADER_OK);
    doe_dx_binding_layout layout = {0};
    assert(native_d3d12_guard_layout(&view,&layout));
    assert(layout.access == DOE_DX_READ_WRITE && layout.view == DOE_DX_STRUCTURED && layout.stride == 4);
    const doe_dx_binding_layout expected = layout;
    DoeCompilerHlslBindingV11 binding = view.hlsl_bindings[0];
    DoeCompilerHlslEntryV11 entry = view.hlsl_entries[0];
    DoeCompilerResourceV4 resource = view.resources[0];
    DoeCompilerEntryV5 stage = view.entries[0];
    doe_shader_view changed = view;
    changed.hlsl_bindings = &binding;
    for (unsigned field = 0; field < 4; ++field) {
        binding = view.hlsl_bindings[0];
        if (field == 0) binding.register_kind = DOE_COMPILER_HLSL_REGISTER_SRV;
        if (field == 1) binding.stride = 8;
        if (field == 2) binding.resource_index = UINT32_MAX;
        if (field == 3) binding.register_space = UINT32_MAX;
        assert(!native_d3d12_guard_layout(&changed,&layout));
        assert(!memcmp(&layout,&expected,sizeof(layout)));
    }
    changed = view;
    changed.hlsl_entries = &entry;
    entry.dispatch_bytes = 16;
    assert(!native_d3d12_guard_layout(&changed,&layout));
    entry = view.hlsl_entries[0];
    entry.flags = DOE_COMPILER_HLSL_ENTRY_WAVES;
    assert(!native_d3d12_guard_layout(&changed,&layout));
    changed = view;
    changed.resources = &resource;
    resource.access = DOE_COMPILER_READ;
    assert(!native_d3d12_guard_layout(&changed,&layout));
    changed = view;
    changed.entries = &stage;
    stage.first_resource_reference = UINT32_MAX;
    assert(!native_d3d12_guard_layout(&changed,&layout));
    stage = view.entries[0];
    stage.workgroup_size[0] = 2;
    assert(!native_d3d12_guard_layout(&changed,&layout));
    assert(native_d3d12_guard_layout(&view,&layout));
    doe_shader_release(shader);
    puts("PASS D3D12 indirect guard compiler/metadata control; no SDK, adapter or GPU execution");
    return 0;
}
