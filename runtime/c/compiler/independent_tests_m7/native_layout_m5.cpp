/* SPDX-License-Identifier: MIT
 * ABI offsets independently derived from the frozen public declarations. */
#include "doe_compiler_v2.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <type_traits>
#define FIELD(type, field, offset) static_assert(offsetof(type, field) == offset, #type "." #field)
#define WIDTH(type, field, width) static_assert(sizeof(((type *)nullptr)->field) == width, #type "." #field " width")
#define RECORD(type, size, alignment) \
    static_assert(sizeof(type) == size && alignof(type) == alignment, #type " layout"); \
    static_assert(std::is_standard_layout<type>::value && std::is_trivially_copyable<type>::value, #type " C-compatible")
static_assert(sizeof(uint32_t) == 4 && sizeof(DoeCompilerStatus) == 4, "native uint32/enums");
static_assert(sizeof(DoeCompilerScalar) == 4 && sizeof(DoeCompilerAccess) == 4, "ABI1 enums");
RECORD(DoeCompilerBinding, 88, 4);
FIELD(DoeCompilerBinding, name, 0); FIELD(DoeCompilerBinding, group, 64);
FIELD(DoeCompilerBinding, binding, 68); FIELD(DoeCompilerBinding, scalar, 72);
FIELD(DoeCompilerBinding, access, 76); FIELD(DoeCompilerBinding, element_stride, 80);
FIELD(DoeCompilerBinding, runtime_sized, 84);
RECORD(DoeCompilerReflection, 784, 4);
FIELD(DoeCompilerReflection, entry_point, 0); FIELD(DoeCompilerReflection, workgroup_size, 64);
FIELD(DoeCompilerReflection, binding_count, 76); FIELD(DoeCompilerReflection, bindings, 80);
RECORD(DoeCompilerMemberV2, 80, 4);
FIELD(DoeCompilerMemberV2, name, 0); FIELD(DoeCompilerMemberV2, scalar, 64);
FIELD(DoeCompilerMemberV2, offset_bytes, 68); FIELD(DoeCompilerMemberV2, size_bytes, 72);
FIELD(DoeCompilerMemberV2, alignment_bytes, 76);
WIDTH(DoeCompilerMemberV2, scalar, 4); WIDTH(DoeCompilerMemberV2, offset_bytes, 4);
WIDTH(DoeCompilerMemberV2, size_bytes, 4); WIDTH(DoeCompilerMemberV2, alignment_bytes, 4);
RECORD(DoeCompilerStructV2, 716, 4);
FIELD(DoeCompilerStructV2, name, 0); FIELD(DoeCompilerStructV2, member_count, 64);
FIELD(DoeCompilerStructV2, size_bytes, 68); FIELD(DoeCompilerStructV2, alignment_bytes, 72);
FIELD(DoeCompilerStructV2, members, 76);
WIDTH(DoeCompilerStructV2, member_count, 4); WIDTH(DoeCompilerStructV2, size_bytes, 4);
WIDTH(DoeCompilerStructV2, alignment_bytes, 4);
RECORD(DoeCompilerBindingV2, 108, 4);
FIELD(DoeCompilerBindingV2, name, 0); FIELD(DoeCompilerBindingV2, group, 64);
FIELD(DoeCompilerBindingV2, binding, 68); FIELD(DoeCompilerBindingV2, kind, 72);
FIELD(DoeCompilerBindingV2, access, 76); FIELD(DoeCompilerBindingV2, scalar, 80);
FIELD(DoeCompilerBindingV2, element_stride, 84); FIELD(DoeCompilerBindingV2, runtime_sized, 88);
FIELD(DoeCompilerBindingV2, minimum_binding_size_bytes, 92);
FIELD(DoeCompilerBindingV2, type_alignment_bytes, 96);
FIELD(DoeCompilerBindingV2, address_space_alignment_bytes, 100);
FIELD(DoeCompilerBindingV2, struct_index, 104);
WIDTH(DoeCompilerBindingV2, group, 4); WIDTH(DoeCompilerBindingV2, binding, 4);
WIDTH(DoeCompilerBindingV2, kind, 4); WIDTH(DoeCompilerBindingV2, access, 4);
WIDTH(DoeCompilerBindingV2, scalar, 4); WIDTH(DoeCompilerBindingV2, element_stride, 4);
WIDTH(DoeCompilerBindingV2, runtime_sized, 4); WIDTH(DoeCompilerBindingV2, minimum_binding_size_bytes, 4);
WIDTH(DoeCompilerBindingV2, type_alignment_bytes, 4); WIDTH(DoeCompilerBindingV2, address_space_alignment_bytes, 4);
WIDTH(DoeCompilerBindingV2, struct_index, 4);
RECORD(DoeCompilerReflectionV2, 6676, 4);
FIELD(DoeCompilerReflectionV2, entry_point, 0); FIELD(DoeCompilerReflectionV2, workgroup_size, 64);
FIELD(DoeCompilerReflectionV2, binding_count, 76); FIELD(DoeCompilerReflectionV2, bindings, 80);
FIELD(DoeCompilerReflectionV2, struct_count, 944); FIELD(DoeCompilerReflectionV2, structs, 948);
WIDTH(DoeCompilerReflectionV2, binding_count, 4); WIDTH(DoeCompilerReflectionV2, struct_count, 4);
FIELD(DoeCompilerWorkspaceRequirements, size_bytes, 0);
FIELD(DoeCompilerWorkspaceRequirements, alignment_bytes, sizeof(size_t));
RECORD(DoeCompilerWorkspaceRequirements, 2*sizeof(size_t), alignof(size_t));
FIELD(DoeCompilerResultV2, word_count, 0); FIELD(DoeCompilerResultV2, reflection, sizeof(size_t));
FIELD(DoeCompilerResult, word_count, 0); FIELD(DoeCompilerResult, reflection, sizeof(size_t));
#if SIZE_MAX == UINT64_MAX
RECORD(DoeCompilerDiagnostic, 216, 8);
FIELD(DoeCompilerDiagnostic, status, 0); FIELD(DoeCompilerDiagnostic, offset, 8);
FIELD(DoeCompilerDiagnostic, line, 16); FIELD(DoeCompilerDiagnostic, column, 20);
FIELD(DoeCompilerDiagnostic, message, 24);
RECORD(DoeCompilerResult, 1008, 8); FIELD(DoeCompilerResult, diagnostic, 792);
RECORD(DoeCompilerResultV2, 6904, 8); FIELD(DoeCompilerResultV2, diagnostic, 6688);
#else
static_assert(sizeof(size_t) == 4, "supported portable size_t widths");
RECORD(DoeCompilerDiagnostic, 208, 4);
FIELD(DoeCompilerDiagnostic, status, 0); FIELD(DoeCompilerDiagnostic, offset, 4);
FIELD(DoeCompilerDiagnostic, line, 8); FIELD(DoeCompilerDiagnostic, column, 12);
FIELD(DoeCompilerDiagnostic, message, 16);
RECORD(DoeCompilerResult, 996, 4); FIELD(DoeCompilerResult, diagnostic, 788);
RECORD(DoeCompilerResultV2, 6888, 4); FIELD(DoeCompilerResultV2, diagnostic, 6680);
#endif
#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "FAIL C++ line %d: %s\n", __LINE__, #condition); return 1; } } while (0)
int main() {
    constexpr char source[] = "struct P{n:u32} @group(0) @binding(0) var<uniform> p:P; @compute @workgroup_size(1) fn main(@builtin(global_invocation_id) g:vec3u){let n=p.n;}";
    DoeCompilerWorkspaceRequirements requirements{};
    CHECK(doe_compiler_workspace_requirements(2, &requirements, sizeof(requirements)) == DOE_COMPILER_OK);
    CHECK(requirements.alignment_bytes <= alignof(std::max_align_t));
    void *workspace = std::malloc(requirements.size_bytes); CHECK(workspace != nullptr);
    uint32_t words[DOE_COMPILER_MAX_WORDS]{}; DoeCompilerResultV2 result{};
    DoeCompilerStatus status = doe_compiler_compile_v2(2, workspace, requirements.size_bytes,
        source, sizeof(source)-1, words, DOE_COMPILER_MAX_WORDS, &result, sizeof(result));
    std::free(workspace); CHECK(status == DOE_COMPILER_OK);
    CHECK(result.reflection.binding_count == 1 && result.reflection.struct_count == 1);
    CHECK(result.reflection.bindings[0].minimum_binding_size_bytes == 4);
    CHECK(result.reflection.structs[0].members[0].offset_bytes == 0);
    std::puts("PASS C++17 ABI1/v2 sizes/offsets/widths and malloc uniform-only consumption");
    return 0;
}
