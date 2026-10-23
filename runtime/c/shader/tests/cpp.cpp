/* SPDX-License-Identifier: MIT */
#include "doe_shader.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
static void *allocate(void *, size_t bytes, size_t) { return std::malloc(bytes); }
static void release(void *, void *memory, size_t, size_t) { std::free(memory); }
int main() {
    assert(doe_shader_retain(nullptr) == DOE_SHADER_INVALID_ARGUMENT);
    doe_shader_options options{};
    options.target = DOE_SHADER_HLSL;
    options.allocator = {nullptr, allocate, release};
    auto &typed = options.capacities.typed;
    typed.source_bytes = 2048;
    typed.tokens = 256;
    typed.statements = 32;
    typed.expressions = 128;
    typed.types = 32;
    typed.members = 16;
    typed.resources = 4;
    typed.access_steps = 64;
    typed.constants = 32;
    typed.constant_edges = 64;
    typed.ir_instructions = 256;
    typed.ir_operands = 256;
    typed.traversal_items = 64;
    typed.expression_depth = 16;
    typed.control_depth = 8;
    typed.type_syntax_depth = 16;
    options.capacities.entries = 4;
    options.capacities.entry_inputs = 16;
    options.capacities.entry_references = 16;
    options.compiler_identity = "C++ HLSL consumer";
    options.compiler_identity_size = 17;
    options.selection = {DOE_COMPILER_SELECT_AUTO, 0u, nullptr, 0};
    const char source[] = "@group(2) @binding(9) var<storage,read_write> output:array<u32>;"
        "@compute @workgroup_size(1) fn main(@builtin(num_workgroups) groups:vec3u){output[0]=groups.x;}";
    doe_shader *shader = nullptr;
    doe_shader_error error{};
    assert(doe_shader_create(&options, source, sizeof(source)-1, &shader, &error) == DOE_SHADER_OK);
    doe_shader_view view{};
    assert(doe_shader_borrow(shader, &view) == DOE_SHADER_OK);
    assert(view.target == DOE_SHADER_HLSL && view.text_size && !view.words);
    assert(view.hlsl_binding_count == 1 && view.hlsl_entry_count == 1);
    assert(view.hlsl_bindings[0].register_space == 2 && view.hlsl_bindings[0].shader_register == 9);
    assert(view.hlsl_entries[0].dispatch_bytes == 16 && !std::strcmp(view.hlsl_entries[0].name, "main"));
    assert(doe_shader_retain(shader) == DOE_SHADER_OK);
    doe_shader_release(shader);
    assert(view.hlsl_entries[0].dispatch_space == 7);
    doe_shader_release(shader);
}
