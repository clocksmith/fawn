/* SPDX-License-Identifier: MIT */
#include "d3d12_frontend.h"
#include "../../shader/include/doe_shader_hlsl_profile.h"
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <wchar.h>

static native_failure loader_failure(DWORD code, const char *operation)
{
    doe_dx_error error = {.status = DOE_DX_UNAVAILABLE, .operation = operation};
    _Static_assert(sizeof(code) == sizeof(error.native_code), "Win32 native error width");
    memcpy(&error.native_code, &code, sizeof(code));
    return native_failure_d3d12(error.status, &error);
}

static native_failure compiler_path(bool fxc,char **out)
{
    /* Dependency lookup is relative to this provider image, never ambient
     * current-directory/PATH search or a fallback compiler selection. The
     * borrowed module is protected by the ordinary executing provider call. */
    static const WCHAR provider_address;
    HMODULE image = NULL;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, &provider_address, &image))
        return loader_failure(GetLastError(), "GetModuleHandleExW.DXC");
    WCHAR path[32768];
    DWORD length = GetModuleFileNameW(image, path, (DWORD)(sizeof(path) / sizeof(*path)));
    if (!length || length >= sizeof(path) / sizeof(*path))
        return loader_failure(length ? ERROR_INSUFFICIENT_BUFFER : GetLastError(), "GetModuleFileNameW.DXC");
    WCHAR *slash = wcsrchr(path, L'\\');
    WCHAR *forward = wcsrchr(path, L'/');
    if (forward && (!slash || forward > slash)) slash = forward;
    const WCHAR *filename = fxc ? L"d3dcompiler_47.dll" : L"dxcompiler.dll";
    const size_t filename_units = wcslen(filename)+1u;
    size_t prefix = slash ? (size_t)(slash - path) + 1 : 0;
    if (!prefix || prefix > sizeof(path) / sizeof(*path) - filename_units)
        return loader_failure(ERROR_BAD_PATHNAME, "provider.DXC.path");
    memcpy(path + prefix, filename, filename_units*sizeof(*filename));
    int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path, -1, NULL, 0, NULL, NULL);
    if (size <= 0) return loader_failure(GetLastError(), "WideCharToMultiByte.DXC.size");
    char *utf8 = malloc((size_t)size);
    if (!utf8) return native_failure_status(NATIVE_OUT_OF_MEMORY);
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path, -1, utf8, size, NULL, NULL) != size) {
        native_failure failure = loader_failure(GetLastError(), "WideCharToMultiByte.DXC.path");
        free(utf8);
        return failure;
    }
    *out = utf8;
    return native_failure_status(NATIVE_OK);
}
#endif

#if defined(_WIN32)
/* The backend keeps one module reference per selected compiler for its whole
 * lifetime, so pipeline creation stops unloading and reloading it. Concurrent
 * first callers race only to publish; the loser drops its own reference. */
static native_failure retain_compiler(native_backend *backend,bool fxc,const char *path)
{
    struct doe_dx_shader_compiler *_Atomic *slot = &backend->value.d3d12.compilers[fxc ? 0 : 1];
    if (atomic_load(slot)) return native_failure_status(NATIVE_OK);
    doe_dx_shader_compiler *retained = NULL;
    doe_dx_shader_error error = {0};
    doe_dx_status status = doe_dx_shader_compiler_retain(path,fxc,&retained,&error);
    doe_dx_error native = {status, error.native_code, error.operation};
    if (status != DOE_DX_OK) return native_failure_d3d12(status,&native);
    doe_dx_shader_compiler *expected = NULL;
    if (!atomic_compare_exchange_strong(slot,&expected,retained)) doe_dx_shader_compiler_release(retained);
    return native_failure_status(NATIVE_OK);
}
#endif

static native_failure compile_stage(native_backend *backend,const native_stage *stage,
    const uint32_t *interstage_mask,doe_dx_shader **out)
{
    if (!backend) return native_failure_status(NATIVE_INVALID_ARGUMENT);
    if (!stage || !stage->entry || !out) return native_failure_status(NATIVE_INVALID_ARGUMENT);
    if (interstage_mask && stage->entry->stage!=DOE_COMPILER_STAGE_VERTEX)
        return native_failure_status(NATIVE_INVALID_ARGUMENT);
#if defined(_WIN32)
    const doe_shader_view *view = &stage->view;
    if (view->target != DOE_SHADER_HLSL || view->target_language_version != 600 ||
        !view->text || !view->text_size || !view->entries || (!stage->imported && !view->hlsl_entries)) return native_failure_status(NATIVE_INVALID_ARGUMENT);
    size_t selected=SIZE_MAX;
    for (size_t i=0;i<view->counts.entries;++i) if (&view->entries[i]==stage->entry) selected=i;
    if (selected==SIZE_MAX) return native_failure_status(NATIVE_INVALID_ARGUMENT);
    const DoeCompilerHlslEntryV11 *entry = NULL;
    for (size_t i = 0; i < view->hlsl_entry_count; ++i) {
        if (view->hlsl_entries[i].entry_index != selected) continue;
        if (entry) return native_failure_status(NATIVE_INVALID_ARGUMENT);
        entry = &view->hlsl_entries[i];
    }
    if (!stage->imported && (!entry || entry->stage != stage->entry->stage ||
        (entry->flags & ~(DOE_COMPILER_HLSL_ENTRY_NATIVE16 | DOE_COMPILER_HLSL_ENTRY_RAW_STORAGE | DOE_COMPILER_HLSL_ENTRY_WAVES | DOE_COMPILER_HLSL_ENTRY_WAVE_COUNTER | DOE_COMPILER_HLSL_ENTRY_ARRAY_VIEWS | DOE_COMPILER_HLSL_ENTRY_DRAW_OFFSETS)) ||
        !memchr(entry->name, 0, sizeof(entry->name)))) return native_failure_status(NATIVE_UNSUPPORTED);
    doe_dx_shader_stage target;
    switch (stage->entry->stage) {
    case DOE_COMPILER_STAGE_COMPUTE: target=DOE_DX_SHADER_COMPUTE; break;
    case DOE_COMPILER_STAGE_VERTEX: target=DOE_DX_SHADER_VERTEX; break;
    case DOE_COMPILER_STAGE_FRAGMENT: target=DOE_DX_SHADER_FRAGMENT; break;
    default: return native_failure_status(NATIVE_UNSUPPORTED);
    }
    const bool native16 = !stage->imported && (entry->flags & DOE_COMPILER_HLSL_ENTRY_NATIVE16) != 0;
    uint32_t model = native16 ? 620u : 600u;
    DoeCompilerHlslProfileV19 profile;
    if (!stage->imported && doe_shader_hlsl_profile(stage->compiled,&profile)) {
        if (profile.entry_index != selected || profile.stage != stage->entry->stage)
            return native_failure_status(NATIVE_UNSUPPORTED);
        model = profile.shader_model;
    }
    char *path = NULL;
    native_failure failure = compiler_path(model == 501u,&path);
    if (failure.status != NATIVE_OK) return failure;
    failure = retain_compiler(backend,model == 501u,path);
    if (failure.status != NATIVE_OK) {
        free(path);
        return failure;
    }
    doe_dx_shader_options options = {.compiler_module_path = path,
        .source = view->text, .source_size = view->text_size, .entry = stage->imported ? stage->name : entry->name,
        .stage = target, .has_entry_index = !stage->imported, .entry_index = entry ? entry->entry_index : 0};
    doe_dx_shader_error error = {0};
    const doe_dx_shader_projection projection={.interstage_mask=interstage_mask ? *interstage_mask : 0};
    const doe_dx_shader_requirements requirements = {.shader_model = model,
        .flags = native16 ? DOE_DX_SHADER_NATIVE16 : 0u};
    doe_dx_status status = doe_dx_shader_compile_required(&options,
        interstage_mask && !stage->imported ? &projection : NULL, &requirements, out, &error);
    doe_dx_error native = {status, error.native_code, error.operation};
    failure = native_failure_d3d12(status, &native);
    /* These exact target bytes/options were produced by the provider after
     * WGSL validation. Rejected compiler input is a provider/compiler failure,
     * not a second diagnosis of invalid application WGSL. Keep its first detail. */
    if (status == DOE_DX_INVALID_ARGUMENT && !stage->imported) failure.status = NATIVE_FAILURE;
    if (status != DOE_DX_OK) {
        size_t size = error.message_size < sizeof(failure.message) - 1 ?
            error.message_size : sizeof(failure.message) - 1;
        memcpy(failure.message, error.message, size);
        failure.message[size] = 0;
    }
    free(path);
    return failure;
#else
    (void)stage;
    (void)interstage_mask;
    (void)out;
    return native_failure_status(NATIVE_UNAVAILABLE);
#endif
}

native_failure native_d3d12_compile_stage(native_backend *backend,const native_stage *stage,doe_dx_shader **out)
{
    return compile_stage(backend,stage,NULL,out);
}

native_failure native_d3d12_compile_vertex(native_backend *backend,const native_stage *stage,
    uint32_t interstage_mask,doe_dx_shader **out)
{
    if (!stage || !stage->entry || stage->entry->stage!=DOE_COMPILER_STAGE_VERTEX)
        return native_failure_status(NATIVE_INVALID_ARGUMENT);
    return compile_stage(backend,stage,&interstage_mask,out);
}

native_failure native_d3d12_compile_compute(native_backend *backend,const native_stage *stage,doe_dx_shader **out)
{
    if (!stage || !stage->entry || stage->entry->stage!=DOE_COMPILER_STAGE_COMPUTE)
        return native_failure_status(NATIVE_INVALID_ARGUMENT);
    return native_d3d12_compile_stage(backend,stage,out);
}

void native_d3d12_compiler_release(doe_dx_shader *shader)
{
#if defined(_WIN32)
    doe_dx_shader_release(shader);
#else
    (void)shader;
#endif
}
