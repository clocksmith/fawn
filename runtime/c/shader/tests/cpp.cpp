/* SPDX-License-Identifier: MIT */
#include "doe_shader.h"
#include "doe_shader_artifact.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
static void *allocate(void *, size_t bytes, size_t) { return std::malloc(bytes); }
static void release(void *, void *memory, size_t, size_t) { std::free(memory); }
int main() {
    assert(doe_shader_artifact_size(nullptr) == 0);
    assert(doe_shader_owned_bytes(nullptr) == 0);
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
    options.selection = {DOE_COMPILER_SELECT_NONE, 0u, nullptr, 0};
    const char module_source[] = "override width:u32;@compute @workgroup_size(width) fn chosen(){}";
    doe_shader *module = nullptr;
    assert(doe_shader_create_module(&options, 4, module_source, sizeof(module_source)-1, &module, &error) == DOE_SHADER_OK);
    doe_shader_module_view metadata{};
    assert(doe_shader_borrow_module(module, &metadata) == DOE_SHADER_OK);
    assert(metadata.entry_count == 1 && metadata.override_count == 1);
    assert(!std::strcmp(metadata.names + metadata.entries[0].name.offset, "chosen"));
    view.text_size = 123;
    assert(doe_shader_borrow(module, &view) == DOE_SHADER_INVALID_ARGUMENT && view.text_size == 123);
    DoeCompilerEntrySelectionV5 selected{DOE_COMPILER_SELECT_NAME, 0u, "chosen", 6};
    const DoeCompilerOverrideValueV13 width{"width", 5, 4.0};
    assert(doe_shader_specialize(module, &selected, &width, 1, &shader, &error) == DOE_SHADER_OK);
    assert(doe_shader_specialization_matches(shader, module, &selected, &width, 1));
    doe_shader_release(module);
    assert(doe_shader_borrow(shader, &view) == DOE_SHADER_OK);
    assert(view.counts.entries == 1 && view.module.selected_entry == 0 && view.entries[0].workgroup_size[0] == 4);
    assert(view.hlsl_entries[0].entry_index == 0 && !std::strcmp(view.hlsl_entries[0].name, "chosen"));
    doe_shader_release(shader);

}
