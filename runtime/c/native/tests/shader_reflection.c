/* SPDX-License-Identifier: MIT
 * Binding reflection extension: per-entry and whole-module bindings come from
 * the compiler's resource closure; unknown entries and invalid modules fail
 * with a diagnostic. */
#include "consumer.h"
#include "doe_native_reflection.h"
#include "doe_native_shader_check.h"

static WGPUShaderModule module(state *s, const char *code)
{
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = (WGPUStringView){code, strlen(code)};
    WGPUShaderModuleDescriptor descriptor = {.nextInChain=&source.chain};
    return wgpuDeviceCreateShaderModule(s->device, &descriptor);
}

static bool has(const DoeNativeShaderBinding *records, size_t count, uint32_t group, uint32_t binding,
    uint32_t kind, uint32_t space, uint32_t access)
{
    for (size_t i = 0; i < count; ++i)
        if (records[i].group == group && records[i].binding == binding && records[i].kind == kind &&
            records[i].addr_space == space && records[i].access == access) return true;
    return false;
}

static void expect_failure(size_t result)
{
    char text[256];
    assert(result == DOE_NATIVE_REFLECTION_FAILED);
    assert(doeNativeCopyLastErrorMessage(text, sizeof(text)) > 0 && text[0]);
}

int main(void)
{
    state s = {0};
    consumer_init(&s);
    WGPUShaderModule shader = module(&s,
        "@group(0) @binding(0) var<storage, read_write> data: array<u32>;\n"
        "@group(0) @binding(1) var<uniform> scale: vec4f;\n"
        "@group(1) @binding(0) var image: texture_2d<f32>;\n"
        "@group(1) @binding(1) var linear_sampler: sampler;\n"
        "@group(2) @binding(0) var output_image: texture_storage_2d<rgba8unorm, write>;\n"
        "@compute @workgroup_size(1) fn main() {\n"
        "  data[0] = u32(scale.x + textureSampleLevel(image, linear_sampler, vec2f(0.5), 0.0).x);\n"
        "}\n"
        "@compute @workgroup_size(1) fn store() {\n"
        "  data[1] = 1u;\n"
        "  textureStore(output_image, vec2i(0), vec4f(1.0));\n"
        "}\n");
    assert(shader);
    DoeNativeShaderBinding records[8];
    size_t count = doeNativeShaderModuleGetBindingsForEntryPoint(shader, "main", 4, records, 8);
    assert(count == 4);
    assert(has(records, count, 0, 0, DOE_NATIVE_BINDING_BUFFER, DOE_NATIVE_SPACE_STORAGE, DOE_NATIVE_ACCESS_READ_WRITE));
    assert(has(records, count, 0, 1, DOE_NATIVE_BINDING_BUFFER, DOE_NATIVE_SPACE_UNIFORM, DOE_NATIVE_ACCESS_READ));
    assert(has(records, count, 1, 0, DOE_NATIVE_BINDING_TEXTURE, DOE_NATIVE_SPACE_HANDLE, DOE_NATIVE_ACCESS_READ));
    assert(has(records, count, 1, 1, DOE_NATIVE_BINDING_SAMPLER, DOE_NATIVE_SPACE_HANDLE, DOE_NATIVE_ACCESS_READ));
    count = doeNativeShaderModuleGetBindingsForEntryPoint(shader, "store", 5, records, 8);
    assert(count == 2);
    assert(has(records, count, 2, 0, DOE_NATIVE_BINDING_STORAGE_TEXTURE, DOE_NATIVE_SPACE_HANDLE, DOE_NATIVE_ACCESS_WRITE));
    /* The whole module lists each (group, binding) once; a short output keeps the full count. */
    assert(doeNativeShaderModuleGetBindings(shader, records, 8) == 5);
    assert(doeNativeShaderModuleGetBindings(shader, records, 1) == 5);
    assert(doeNativeShaderModuleGetBindings(shader, NULL, 0) == 5);
    expect_failure(doeNativeShaderModuleGetBindingsForEntryPoint(shader, "missing", 7, records, 8));
    expect_failure(doeNativeShaderModuleGetBindingsForEntryPoint(shader, NULL, 0, records, 8));
    expect_failure(doeNativeShaderModuleGetBindings(NULL, records, 8));

    WGPUShaderModule empty = module(&s, "@compute @workgroup_size(1) fn main() {}");
    assert(doeNativeShaderModuleGetBindings(empty, records, 8) == 0);
    assert(doeNativeShaderModuleGetBindingsForEntryPoint(empty, "main", 4, records, 8) == 0);
    char text[8];
    assert(doeNativeCopyLastErrorMessage(text, sizeof(text)) == 0);

    wgpuDevicePushErrorScope(s.device, WGPUErrorFilter_Validation);
    WGPUShaderModule invalid = module(&s, "not WGSL");
    expect_validation(&s);
    expect_failure(doeNativeShaderModuleGetBindings(invalid, records, 8));
    expect_failure(doeNativeShaderModuleGetBindingsForEntryPoint(invalid, "main", 4, records, 8));

    wgpuShaderModuleRelease(invalid);
    wgpuShaderModuleRelease(empty);
    wgpuShaderModuleRelease(shader);
    consumer_finish(&s);
    printf("PASS binding reflection reports per-entry and module bindings and rejects unknown entries %s\n",
        adapter_class_scope());
    return 0;
}
