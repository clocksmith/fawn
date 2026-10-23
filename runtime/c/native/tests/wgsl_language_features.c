/* SPDX-License-Identifier: MIT
 * Every pinned WGSL language feature has a probe compiled through the public
 * shader-module and compute-pipeline path on the selected Vulkan device. A
 * reported feature's probe must compile and create a real pipeline both with
 * and without its `requires` directive; every unreported feature's probe must
 * be rejected at some public boundary. The table cannot drift silently. */
#include "consumer.h"
#include <string.h>

typedef struct probe {
    WGPUWGSLLanguageFeatureName feature;
    const char *name;
    const char *directives; /* Needed enables, placed before the requires directive. */
    const char *body;
    const char *control; /* Same shape without the feature: must always be accepted. */
} probe;

static const probe probes[] = {
    {WGPUWGSLLanguageFeatureName_ReadonlyAndReadwriteStorageTextures, "readonly_and_readwrite_storage_textures", "",
        "@group(0) @binding(0) var read_image: texture_storage_2d<r32float, read>;"
        "@group(0) @binding(1) var write_image: texture_storage_2d<r32float, read_write>;"
        "@compute @workgroup_size(1) fn main() {"
        "let value = textureLoad(read_image, vec2i(0, 0)) + textureLoad(write_image, vec2i(0, 0));"
        "textureStore(write_image, vec2i(0, 0), value); }",
        "@group(0) @binding(0) var target_image: texture_storage_2d<r32float, write>;"
        "@compute @workgroup_size(1) fn main() { textureStore(target_image, vec2i(0, 0), vec4f(1.0)); }"},
    {WGPUWGSLLanguageFeatureName_Packed4x8IntegerDotProduct, "packed_4x8_integer_dot_product", "",
        "@group(0) @binding(0) var<storage, read_write> data: array<u32, 4>;"
        "@compute @workgroup_size(1) fn main() {"
        "let a = data[1]; let b = data[2];"
        "data[0] = bitcast<u32>(dot4I8Packed(a, b)) + dot4U8Packed(a, b) + pack4xI8(vec4i(-1, 2, -3, 4))"
        " + pack4xU8(vec4u(1u, 2u, 3u, 4u)) + pack4xI8Clamp(vec4i(-300, 2, 3, 300))"
        " + pack4xU8Clamp(vec4u(1u, 2u, 3u, 300u)) + bitcast<u32>(unpack4xI8(a).x) + unpack4xU8(b).w; }",
        "@group(0) @binding(0) var<storage, read_write> data: array<u32, 4>;"
        "@compute @workgroup_size(1) fn main() { data[0] = data[1] + data[2]; }"},
    {WGPUWGSLLanguageFeatureName_UnrestrictedPointerParameters, "unrestricted_pointer_parameters", "",
        "@group(0) @binding(0) var<storage, read_write> data: array<u32, 4>;"
        "@group(0) @binding(1) var<uniform> scale: vec4u;"
        "var<workgroup> shared_value: u32;"
        "fn combine(destination: ptr<storage, array<u32, 4>, read_write>, w: ptr<workgroup, u32>, u: ptr<uniform, vec4u>) {"
        "(*destination)[0] = *w * (*u).x; }"
        "@compute @workgroup_size(1) fn main() { shared_value = 3u; combine(&data, &shared_value, &scale); }",
        "@group(0) @binding(0) var<storage, read_write> data: array<u32, 4>;"
        "fn store(p: ptr<function, u32>) { *p = 3u; }"
        "@compute @workgroup_size(1) fn main() { var value = 0u; store(&value); data[0] = value; }"},
    {WGPUWGSLLanguageFeatureName_PointerCompositeAccess, "pointer_composite_access", "",
        "struct Record { first: u32, rest: array<u32, 2> }"
        "@group(0) @binding(0) var<storage, read_write> record: Record;"
        "@compute @workgroup_size(1) fn main() { var copy = record; let p = &copy; p.first = p.rest[1]; record = copy; }",
        "struct Record { first: u32, rest: array<u32, 2> }"
        "@group(0) @binding(0) var<storage, read_write> record: Record;"
        "@compute @workgroup_size(1) fn main() { var copy = record; let p = &copy; (*p).first = (*p).rest[1]; record = copy; }"},
    {WGPUWGSLLanguageFeatureName_UniformBufferStandardLayout, "uniform_buffer_standard_layout", "",
        "struct Packed { values: array<f32, 4> }"
        "@group(0) @binding(0) var<uniform> packed: Packed;"
        "@group(0) @binding(1) var<storage, read_write> data: array<f32, 4>;"
        "@compute @workgroup_size(1) fn main() { data[0] = packed.values[1]; }",
        "struct Padded { values: array<vec4f, 4> }"
        "@group(0) @binding(0) var<uniform> padded: Padded;"
        "@group(0) @binding(1) var<storage, read_write> data: array<f32, 4>;"
        "@compute @workgroup_size(1) fn main() { data[0] = padded.values[1].x; }"},
    {WGPUWGSLLanguageFeatureName_SubgroupId, "subgroup_id", "enable subgroups;",
        "@group(0) @binding(0) var<storage, read_write> data: array<u32, 4>;"
        "@compute @workgroup_size(64) fn main(@builtin(subgroup_id) id: u32) { if (id == 0u) { data[0] = 1u; } }",
        "@group(0) @binding(0) var<storage, read_write> data: array<u32, 4>;"
        "@compute @workgroup_size(64) fn main(@builtin(local_invocation_index) id: u32) { if (id == 0u) { data[0] = 1u; } }"},
    {WGPUWGSLLanguageFeatureName_TextureAndSamplerLet, "texture_and_sampler_let", "",
        "@group(0) @binding(0) var image: texture_2d<f32>;"
        "@group(0) @binding(1) var image_sampler: sampler;"
        "@group(0) @binding(2) var<storage, read_write> data: array<f32, 4>;"
        "@compute @workgroup_size(1) fn main() { let t = image; let s = image_sampler;"
        "data[0] = textureSampleLevel(t, s, vec2f(0.5, 0.5), 0.0).x; }",
        "@group(0) @binding(0) var image: texture_2d<f32>;"
        "@group(0) @binding(1) var image_sampler: sampler;"
        "@group(0) @binding(2) var<storage, read_write> data: array<f32, 4>;"
        "@compute @workgroup_size(1) fn main() {"
        "data[0] = textureSampleLevel(image, image_sampler, vec2f(0.5, 0.5), 0.0).x; }"},
    {WGPUWGSLLanguageFeatureName_SubgroupUniformity, "subgroup_uniformity", "enable subgroups;",
        "@group(0) @binding(0) var<storage, read_write> data: array<u32, 4>;"
        "@compute @workgroup_size(64) fn main(@builtin(subgroup_size) size: u32) {"
        "if (size > 0u) { data[0] = subgroupAdd(1u); } }",
        "@group(0) @binding(0) var<storage, read_write> data: array<u32, 4>;"
        "@compute @workgroup_size(64) fn main(@builtin(local_invocation_index) i: u32) { if (i == 0u) { data[0] = 1u; } }"},
    {WGPUWGSLLanguageFeatureName_TextureFormatsTier1, "texture_formats_tier1", "",
        "@group(0) @binding(0) var target_image: texture_storage_2d<r8unorm, write>;"
        "@compute @workgroup_size(1) fn main() { textureStore(target_image, vec2i(0, 0), vec4f(1.0)); }",
        "@group(0) @binding(0) var target_image: texture_storage_2d<rgba8unorm, write>;"
        "@compute @workgroup_size(1) fn main() { textureStore(target_image, vec2i(0, 0), vec4f(1.0)); }"},
    {WGPUWGSLLanguageFeatureName_LinearIndexing, "linear_indexing", "",
        "@group(0) @binding(0) var<storage, read_write> data: array<u32, 64>;"
        "@compute @workgroup_size(4) fn main(@builtin(global_invocation_index) i: u32,"
        " @builtin(workgroup_index) g: u32) { data[i] = g; }",
        "@group(0) @binding(0) var<storage, read_write> data: array<u32, 64>;"
        "@compute @workgroup_size(4) fn main(@builtin(local_invocation_index) i: u32,"
        " @builtin(workgroup_id) g: vec3u) { data[i] = g.x; }"},
    {WGPUWGSLLanguageFeatureName_ImmediateAddressSpace, "immediate_address_space", "",
        "var<immediate> constants: vec4u;"
        "@group(0) @binding(0) var<storage, read_write> data: array<u32, 4>;"
        "@compute @workgroup_size(1) fn main() { data[0] = constants.x; }",
        "@group(0) @binding(1) var<uniform> constants: vec4u;"
        "@group(0) @binding(0) var<storage, read_write> data: array<u32, 4>;"
        "@compute @workgroup_size(1) fn main() { data[0] = constants.x; }"},
    {WGPUWGSLLanguageFeatureName_BufferView, "buffer_view", "",
        "@group(0) @binding(0) var<storage, read_write> bytes: buffer<16>;"
        "@compute @workgroup_size(1) fn main() { let p = bufferView<u32>(&bytes, 0u); *p = 1u; }",
        "@group(0) @binding(0) var<storage, read_write> words: array<u32>;"
        "@compute @workgroup_size(1) fn main() { words[0] = arrayLength(&words); }"},
};

typedef enum outcome { ACCEPTED, MODULE_REJECTED, PIPELINE_REJECTED } outcome;
static const char *const outcome_names[] = {"pipeline created", "shader module rejected", "pipeline rejected"};

typedef struct diagnostic {
    unsigned errors;
    char text[160]; /* First compiler diagnostic or typed device error. */
} diagnostic;
static void remember(diagnostic *d, WGPUStringView message)
{
    if (d->text[0] || !message.data) return;
    size_t length = message.length == WGPU_STRLEN ? strlen(message.data) : message.length;
    if (length >= sizeof(d->text)) length = sizeof(d->text) - 1;
    memcpy(d->text, message.data, length);
    d->text[length] = 0;
}
static void compilation(WGPUCompilationInfoRequestStatus status, const WGPUCompilationInfo *info,
    void *userdata, void *unused)
{
    (void)unused;
    assert(status == WGPUCompilationInfoRequestStatus_Success && info);
    diagnostic *d = userdata;
    for (size_t i = 0; i < info->messageCount; ++i)
        if (info->messages[i].type == WGPUCompilationMessageType_Error) {
            ++d->errors;
            remember(d, info->messages[i].message);
        }
}
typedef struct popped_error {
    WGPUErrorType type;
    diagnostic *diagnostic;
} popped_error;
static void popped(WGPUPopErrorScopeStatus status, WGPUErrorType type, WGPUStringView message,
    void *userdata, void *unused)
{
    (void)unused;
    assert(status == WGPUPopErrorScopeStatus_Success);
    popped_error *out = userdata;
    out->type = type;
    if (type != WGPUErrorType_NoError) remember(out->diagnostic, message);
}
static WGPUErrorType pop(state *s, diagnostic *d)
{
    popped_error result = {WGPUErrorType_Unknown, d};
    WGPUPopErrorScopeCallbackInfo callback = {.mode=WGPUCallbackMode_AllowSpontaneous,.callback=popped,.userdata1=&result};
    assert(wgpuDevicePopErrorScope(s->device, callback).id && result.type != WGPUErrorType_Unknown);
    return result.type;
}

/* Public shader-module compilation, then a real compute pipeline on this device. */
static outcome run_source(state *s, const char *directives, const char *requirement, const char *body,
    diagnostic *d)
{
    char source[2048];
    int length = snprintf(source, sizeof(source), "%s%s%s%s%s", directives, requirement ? "requires " : "",
        requirement ? requirement : "", requirement ? ";" : "", body);
    assert(length > 0 && (size_t)length < sizeof(source));
    WGPUShaderSourceWGSL wgsl = WGPU_SHADER_SOURCE_WGSL_INIT;
    wgsl.code = (WGPUStringView){source, (size_t)length};
    WGPUShaderModuleDescriptor descriptor = {.nextInChain=&wgsl.chain};
    *d = (diagnostic){0};
    wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Validation);
    wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Internal);
    wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_OutOfMemory);
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(s->device, &descriptor);
    if (module) {
        WGPUCompilationInfoCallbackInfo info = {.mode=WGPUCallbackMode_AllowSpontaneous,.callback=compilation,.userdata1=d};
        assert(wgpuShaderModuleGetCompilationInfo(module, info).id);
    }
    outcome result = MODULE_REJECTED;
    if (module && !d->errors) {
        WGPUComputePipelineDescriptor pipeline_desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
        pipeline_desc.compute.module = module;
        WGPUComputePipeline pipeline = wgpuDeviceCreateComputePipeline(s->device, &pipeline_desc);
        result = pipeline ? ACCEPTED : PIPELINE_REJECTED;
        if (pipeline) wgpuComputePipelineRelease(pipeline);
    }
    if (module) wgpuShaderModuleRelease(module);
    WGPUErrorType oom = pop(s, d), internal = pop(s, d), validation = pop(s, d);
    /* Acceptance must be silent; any rejection must surface a typed error or diagnostic. */
    if (result == ACCEPTED) assert(oom == WGPUErrorType_NoError && internal == WGPUErrorType_NoError &&
        validation == WGPUErrorType_NoError);
    else assert(d->errors || oom != WGPUErrorType_NoError || internal != WGPUErrorType_NoError ||
        validation != WGPUErrorType_NoError);
    return result;
}

int main(void)
{
    state s = {0};
    consumer_init(&s);
    WGPUSupportedWGSLLanguageFeatures reported = WGPU_SUPPORTED_WGSL_LANGUAGE_FEATURES_INIT;
    wgpuInstanceGetWGSLLanguageFeatures(s.instance, &reported);
    assert(reported.featureCount == 0 || reported.features);
    unsigned reported_count = 0;
    for (size_t i = 0; i < sizeof(probes) / sizeof(probes[0]); ++i) {
        const probe *p = &probes[i];
        diagnostic control_diagnostic, plain_diagnostic, directive_diagnostic;
        /* The control proves the probe's surrounding shape is admitted here, so a
         * rejection below is attributable to the feature, not to the harness. */
        outcome control = run_source(&s, "", NULL, p->control, &control_diagnostic);
        if (control != ACCEPTED) fprintf(stderr, "%s control: %s\n", p->name, control_diagnostic.text);
        assert(control == ACCEPTED);
        outcome plain = run_source(&s, p->directives, NULL, p->body, &plain_diagnostic);
        outcome directive = run_source(&s, p->directives, p->name, p->body, &directive_diagnostic);
        bool listed = false;
        for (size_t j = 0; j < reported.featureCount; ++j) listed |= reported.features[j] == p->feature;
        bool has = wgpuInstanceHasWGSLLanguageFeature(s.instance, p->feature);
        printf("%-40s reported=%-3s plain: %-22s requires: %-22s first diagnostic: %s | %s\n", p->name,
            listed ? "yes" : "no", outcome_names[plain], outcome_names[directive], plain_diagnostic.text,
            directive_diagnostic.text);
        assert(listed == has);
        if (listed) {
            ++reported_count;
            assert(plain == ACCEPTED && directive == ACCEPTED);
        } else assert(plain != ACCEPTED || directive != ACCEPTED);
    }
    /* Every reported name is a pinned, probed feature; no duplicates or extras. */
    assert(reported.featureCount == reported_count);
    assert(!wgpuInstanceHasWGSLLanguageFeature(s.instance, (WGPUWGSLLanguageFeatureName)0));
    assert(!wgpuInstanceHasWGSLLanguageFeature(s.instance, WGPUWGSLLanguageFeatureName_Force32));
    assert(!wgpuInstanceHasWGSLLanguageFeature(NULL, WGPUWGSLLanguageFeatureName_PointerCompositeAccess));
    wgpuSupportedWGSLLanguageFeaturesFreeMembers(reported);
    WGPUSupportedWGSLLanguageFeatures none = WGPU_SUPPORTED_WGSL_LANGUAGE_FEATURES_INIT;
    wgpuInstanceGetWGSLLanguageFeatures(NULL, &none);
    assert(none.featureCount == 0 && !none.features);
    wgpuSupportedWGSLLanguageFeaturesFreeMembers(none);
    wgpuInstanceGetWGSLLanguageFeatures(s.instance, NULL);
    consumer_finish(&s);
    printf("PASS WGSL language features: %u reported of %zu pinned; every probe's control created a real pipeline, "
        "reported probes pass with and without requires, each unreported probe is rejected at the public "
        "module or pipeline boundary, empty/NULL outputs owned correctly %s\n",
        reported_count, sizeof(probes) / sizeof(probes[0]), adapter_class_scope());
    return 0;
}
