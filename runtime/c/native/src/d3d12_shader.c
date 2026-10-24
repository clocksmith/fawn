/* SPDX-License-Identifier: MIT */
#include "d3d12_frontend.h"
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

static native_failure compiler_path(char **out)
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
    static const WCHAR filename[] = L"dxcompiler.dll";
    size_t prefix = slash ? (size_t)(slash - path) + 1 : 0;
    if (!prefix || prefix > sizeof(path) / sizeof(*path) - sizeof(filename) / sizeof(*filename))
        return loader_failure(ERROR_BAD_PATHNAME, "provider.DXC.path");
    memcpy(path + prefix, filename, sizeof(filename));
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

static native_failure compile_stage(const native_stage *stage,const uint32_t *interstage_mask,doe_dx_shader **out)
{
    if (!stage || !stage->entry || !out) return native_failure_status(NATIVE_INVALID_ARGUMENT);
    if (interstage_mask && stage->entry->stage!=DOE_COMPILER_STAGE_VERTEX)
        return native_failure_status(NATIVE_INVALID_ARGUMENT);
#if defined(_WIN32)
    const doe_shader_view *view = &stage->view;
    if (view->target != DOE_SHADER_HLSL || view->target_language_version != 600 ||
        !view->text || !view->text_size || !view->entries || !view->hlsl_entries) return native_failure_status(NATIVE_INVALID_ARGUMENT);
    size_t selected=SIZE_MAX;
    for (size_t i=0;i<view->counts.entries;++i) if (&view->entries[i]==stage->entry) selected=i;
    if (selected==SIZE_MAX) return native_failure_status(NATIVE_INVALID_ARGUMENT);
    const DoeCompilerHlslEntryV11 *entry = NULL;
    for (size_t i = 0; i < view->hlsl_entry_count; ++i) {
        if (view->hlsl_entries[i].entry_index != selected) continue;
        if (entry) return native_failure_status(NATIVE_INVALID_ARGUMENT);
        entry = &view->hlsl_entries[i];
    }
    if (!entry || entry->stage != stage->entry->stage || entry->flags ||
        !memchr(entry->name, 0, sizeof(entry->name))) return native_failure_status(NATIVE_UNSUPPORTED);
    doe_dx_shader_stage target;
    switch (entry->stage) {
    case DOE_COMPILER_STAGE_COMPUTE: target=DOE_DX_SHADER_COMPUTE; break;
    case DOE_COMPILER_STAGE_VERTEX: target=DOE_DX_SHADER_VERTEX; break;
    case DOE_COMPILER_STAGE_FRAGMENT: target=DOE_DX_SHADER_FRAGMENT; break;
    default: return native_failure_status(NATIVE_UNSUPPORTED);
    }
    char *path = NULL;
    native_failure failure = compiler_path(&path);
    if (failure.status != NATIVE_OK) return failure;
    doe_dx_shader_options options = {.compiler_module_path = path,
        .source = view->text, .source_size = view->text_size, .entry = entry->name,
        .stage = target, .has_entry_index = true, .entry_index = entry->entry_index};
    doe_dx_shader_error error = {0};
    const doe_dx_shader_projection projection={.interstage_mask=interstage_mask ? *interstage_mask : 0};
    doe_dx_status status = interstage_mask ? doe_dx_shader_compile_projected(&options,&projection,out,&error) :
        doe_dx_shader_compile(&options,out,&error);
    doe_dx_error native = {status, error.native_code, error.operation};
    failure = native_failure_d3d12(status, &native);
    /* These exact target bytes/options were produced by the provider after
     * WGSL validation. A rejected DXC input is a provider/compiler failure,
     * not a second diagnosis of invalid application WGSL. Keep its first detail. */
    if (status == DOE_DX_INVALID_ARGUMENT) failure.status = NATIVE_FAILURE;
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

native_failure native_d3d12_compile_stage(const native_stage *stage,doe_dx_shader **out)
{
    return compile_stage(stage,NULL,out);
}

native_failure native_d3d12_compile_vertex(const native_stage *stage,uint32_t interstage_mask,doe_dx_shader **out)
{
    if (!stage || !stage->entry || stage->entry->stage!=DOE_COMPILER_STAGE_VERTEX)
        return native_failure_status(NATIVE_INVALID_ARGUMENT);
    return compile_stage(stage,&interstage_mask,out);
}

native_failure native_d3d12_compile_compute(const native_stage *stage,doe_dx_shader **out)
{
    if (!stage || !stage->entry || stage->entry->stage!=DOE_COMPILER_STAGE_COMPUTE)
        return native_failure_status(NATIVE_INVALID_ARGUMENT);
    return native_d3d12_compile_stage(stage,out);
}

void native_d3d12_compiler_release(doe_dx_shader *shader)
{
#if defined(_WIN32)
    doe_dx_shader_release(shader);
#else
    (void)shader;
#endif
}
