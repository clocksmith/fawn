/* SPDX-License-Identifier: MIT */
#include "doe_d3d12_shader.h"
#include "../../../compiler/include/doe_compiler_hlsl.h"
#ifndef _WIN32
#error This official DXC adapter requires Windows and the official DXC SDK.
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0602
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <unknwn.h>
#include <dxcapi.h>
#include <d3dcompiler.h>
#include <cerrno>
#include <cfenv>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#if defined(_M_X64) || defined(__x86_64__) || defined(_M_IX86) || defined(__i386__)
#include <xmmintrin.h>
#else
#error DXC host-state preservation currently requires an x86 Windows host.
#endif

struct doe_dx_shader {
    HMODULE module;
    IDxcBlob *bytecode;
    ID3DBlob *fxc_bytecode;
};

struct compile_state {
    HMODULE module;
    IDxcCompiler3 *compiler;
    IDxcResult *result;
    IDxcBlobUtf8 *diagnostic;
    IDxcBlob *bytecode;
    ID3DBlob *fxc_bytecode, *fxc_diagnostic;
    wchar_t *path;
};

static void cleanup(compile_state *state)
{
    if (state->bytecode) state->bytecode->Release();
    if (state->fxc_bytecode) state->fxc_bytecode->Release();
    if (state->fxc_diagnostic) state->fxc_diagnostic->Release();
    if (state->diagnostic) state->diagnostic->Release();
    if (state->result) state->result->Release();
    if (state->compiler) state->compiler->Release();
    if (state->module && !FreeLibrary(state->module)) std::abort();
    std::free(state->path);
}

static doe_dx_status report(doe_dx_shader_error *error, doe_dx_status status,
    int32_t native_code, const char *operation, const char *message, size_t length)
{
    *error = {};
    error->status = status;
    error->native_code = native_code;
    error->operation = operation;
    size_t copied = length < sizeof(error->message) - 1 ? length : sizeof(error->message) - 1;
    /* DXC owns valid UTF-8; retain a complete prefix if its diagnostic is cut. */
    if (copied < length) {
        while (copied && (static_cast<unsigned char>(message[copied]) & 0xc0u) == 0x80u) --copied;
    }
    if (copied) std::memcpy(error->message, message, copied);
    error->message[copied] = 0;
    error->message_size = copied;
    error->truncated = copied != length;
    return status;
}

static doe_dx_status reject(doe_dx_shader_error *error, const char *message)
{
    return report(error, DOE_DX_INVALID_ARGUMENT, static_cast<int32_t>(E_INVALIDARG),
        "DXC.options", message, std::strlen(message));
}

static doe_dx_status failed(compile_state *state, doe_dx_shader_error *error,
    doe_dx_status status, int32_t native_code, const char *operation)
{
    const char *message = operation;
    size_t length = std::strlen(operation);
    if (state->fxc_diagnostic && state->fxc_diagnostic->GetBufferPointer() &&
        state->fxc_diagnostic->GetBufferSize()) {
        message = static_cast<const char *>(state->fxc_diagnostic->GetBufferPointer());
        length = state->fxc_diagnostic->GetBufferSize();
        if (message[length-1] == 0) --length;
    }
    if (state->result) {
        /* Best-effort diagnostics never replace the first failure/code. */
        /* The SDK macro uses Microsoft UUID metadata. Acknowledge only that
         * required expression extension, retaining its typed output checks. */
        HRESULT retrieved = state->result->GetOutput(DXC_OUT_ERRORS,
            __extension__ IID_PPV_ARGS(&state->diagnostic), nullptr);
        if (SUCCEEDED(retrieved) && state->diagnostic && state->diagnostic->GetStringLength()) {
            message = state->diagnostic->GetStringPointer();
            length = state->diagnostic->GetStringLength();
        }
    }
    report(error, status, native_code, operation, message, length);
    cleanup(state);
    return status;
}

static doe_dx_status native_status(HRESULT result)
{
    return result == E_OUTOFMEMORY ? DOE_DX_OUT_OF_MEMORY : DOE_DX_NATIVE_FAILURE;
}

static bool identifier(const char *name, wchar_t output[64])
{
    if (!name || !name[0]) return false;
    for (size_t i = 0; i < 64; ++i) {
        unsigned char c = static_cast<unsigned char>(name[i]);
        if (!c) { output[i] = 0; return true; }
        bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
        if ((!letter && !(i && c >= '0' && c <= '9')) || i == 63) return false;
        output[i] = static_cast<wchar_t>(c);
    }
    return false;
}

static bool absolute_path(const wchar_t *path)
{
    size_t length = std::wcslen(path);
    bool letter = (path[0] >= L'a' && path[0] <= L'z') || (path[0] >= L'A' && path[0] <= L'Z');
    if (length >= 3 && letter && path[1] == L':' && (path[2] == L'\\' || path[2] == L'/')) return true;
    if (length < 5 || path[0] != L'\\' || path[1] != L'\\') return false;
    /* UNC and extended absolute paths need an actual server/device component. */
    const wchar_t *separator = std::wcschr(path + 2, L'\\');
    return separator && separator != path + 2 && separator[1] != 0;
}

static doe_dx_status load_compiler(const doe_dx_shader_options *options,
    compile_state *state,doe_dx_shader_error *error,bool fxc)
{
    const int path_size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        options->compiler_module_path, -1, nullptr, 0);
    if (!path_size || path_size > 32767)
        return reject(error,fxc ? "invalid FXC UTF-8 module path" : "invalid DXC UTF-8 module path");
    state->path = static_cast<wchar_t *>(std::calloc(static_cast<size_t>(path_size), sizeof(wchar_t)));
    if (!state->path) return failed(state,error,DOE_DX_OUT_OF_MEMORY,
        static_cast<int32_t>(E_OUTOFMEMORY),fxc ? "FXC.module_path.allocate" : "DXC.module_path.allocate");
    if (!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,options->compiler_module_path,
            -1,state->path,path_size) || !absolute_path(state->path)) {
        cleanup(state);
        return reject(error,fxc ? "FXC module path must be fully qualified UTF-8" :
            "DXC module path must be fully qualified UTF-8");
    }
    state->module = LoadLibraryExW(state->path,nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!state->module) return failed(state,error,DOE_DX_UNAVAILABLE,
        static_cast<int32_t>(GetLastError()),fxc ? "LoadLibraryExW(FXC)" : "LoadLibraryExW(DXC)");
    return DOE_DX_OK;
}

static doe_dx_status compile_fxc(const doe_dx_shader_options *options,
    doe_dx_shader **out,doe_dx_shader_error *error)
{
    compile_state state = {};
    const doe_dx_status loaded = load_compiler(options,&state,error,true);
    if (loaded != DOE_DX_OK) return loaded;
    const FARPROC symbol = GetProcAddress(state.module,"D3DCompile");
    if (!symbol) return failed(&state,error,DOE_DX_UNAVAILABLE,
        static_cast<int32_t>(GetLastError()),"GetProcAddress(D3DCompile)");
    decltype(&D3DCompile) compile;
    static_assert(sizeof(compile) == sizeof(symbol),"Windows FXC procedure pointer ABI");
    std::memcpy(&compile,&symbol,sizeof(compile));
    char selected[16];
    D3D_SHADER_MACRO definitions[2] = {};
    if (options->has_entry_index) {
        const int length = std::snprintf(selected,sizeof(selected),"%u",
            static_cast<unsigned>(options->entry_index));
        if (length <= 0 || static_cast<size_t>(length) >= sizeof(selected))
            return failed(&state,error,DOE_DX_RANGE,static_cast<int32_t>(E_INVALIDARG),"FXC.entry_index");
        definitions[0] = {"DOE_HLSL_ENTRY",selected};
    }
    /* Preserve strict arithmetic and warnings; match the existing optimized
     * DXC policy rather than expose application-controlled compiler flags. */
    const UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS |
        D3DCOMPILE_IEEE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
    const HRESULT result = compile(options->source,options->source_size,nullptr,
        options->has_entry_index ? definitions : nullptr,nullptr,options->entry,"cs_5_1",
        flags,0,&state.fxc_bytecode,&state.fxc_diagnostic);
    if (FAILED(result)) return failed(&state,error,
        result == E_OUTOFMEMORY ? DOE_DX_OUT_OF_MEMORY : DOE_DX_INVALID_ARGUMENT,
        static_cast<int32_t>(result),"FXC.compile");
    if (!state.fxc_bytecode || !state.fxc_bytecode->GetBufferPointer() || !state.fxc_bytecode->GetBufferSize())
        return failed(&state,error,DOE_DX_NATIVE_FAILURE,static_cast<int32_t>(E_FAIL),"FXC.bytecode");
    doe_dx_shader *shader = static_cast<doe_dx_shader *>(std::calloc(1,sizeof(*shader)));
    if (!shader) return failed(&state,error,DOE_DX_OUT_OF_MEMORY,
        static_cast<int32_t>(E_OUTOFMEMORY),"FXC.bytecode.allocate");
    shader->module = state.module;
    shader->fxc_bytecode = state.fxc_bytecode;
    state.module = nullptr;
    state.fxc_bytecode = nullptr;
    cleanup(&state);
    *out = shader;
    return report(error,DOE_DX_OK,0,nullptr,nullptr,0);
}

static doe_dx_status compile_shader(const doe_dx_shader_options *options,
    const doe_dx_shader_projection *projection,bool require_projection,
    const doe_dx_shader_requirements *requirements,
    doe_dx_shader **out,doe_dx_shader_error *error)
{
    wchar_t entry[64];
    if (!options || !out || !options->compiler_module_path || !options->source ||
        !options->source_size || !identifier(options->entry, entry) ||
        std::memchr(options->source, 0, options->source_size))
        return reject(error, "invalid DXC source, module path, entry or output");
    if ((require_projection && !projection) || (projection &&
        (projection->reserved || options->stage!=DOE_DX_SHADER_VERTEX)))
        return reject(error,"projection requires a vertex stage and zero reserved fields");
    const doe_dx_shader_requirements legacy = {600, 0, {0, 0}};
    if (!requirements) requirements = &legacy;
    if ((requirements->shader_model != 501 && requirements->shader_model != 600 && requirements->shader_model != 620) ||
        (requirements->flags & ~DOE_DX_SHADER_NATIVE16) || requirements->reserved[0] ||
        requirements->reserved[1] ||
        ((requirements->flags & DOE_DX_SHADER_NATIVE16) && requirements->shader_model != 620))
        return reject(error, "invalid or inconsistent DXC shader requirements");
    if (requirements->shader_model == 501) {
        if (options->stage != DOE_DX_SHADER_COMPUTE || projection || requirements->flags)
            return reject(error,"SM5.1 requires an explicit compute profile without projection/native16");
        return compile_fxc(options,out,error);
    }
    const bool model62 = requirements->shader_model == 620;
    const wchar_t *profile;
    switch (options->stage) {
    case DOE_DX_SHADER_COMPUTE: profile = model62 ? L"cs_6_2" : L"cs_6_0"; break;
    case DOE_DX_SHADER_VERTEX: profile = model62 ? L"vs_6_2" : L"vs_6_0"; break;
    case DOE_DX_SHADER_FRAGMENT: profile = model62 ? L"ps_6_2" : L"ps_6_0"; break;
    default: return reject(error, "unsupported DXC shader stage");
    }
    compile_state state = {};
    const doe_dx_status loaded = load_compiler(options,&state,error,false);
    if (loaded != DOE_DX_OK) return loaded;
    FARPROC symbol = GetProcAddress(state.module, "DxcCreateInstance");
    if (!symbol) return failed(&state, error, DOE_DX_UNAVAILABLE,
        static_cast<int32_t>(GetLastError()), "GetProcAddress(DxcCreateInstance)");
    DxcCreateInstanceProc create;
    static_assert(sizeof(create) == sizeof(symbol), "Windows DXC procedure pointer ABI");
    std::memcpy(&create, &symbol, sizeof(create));
    HRESULT result = create(CLSID_DxcCompiler, __extension__ IID_PPV_ARGS(&state.compiler));
    if (FAILED(result) || !state.compiler) return failed(&state, error,
        native_status(FAILED(result) ? result : E_FAIL),
        static_cast<int32_t>(FAILED(result) ? result : E_FAIL), "DxcCreateInstance(IDxcCompiler3)");
    const wchar_t *arguments[16] = {L"-T", profile, L"-E", entry, L"-HV", L"2021",
        DXC_ARG_ENABLE_STRICTNESS, DXC_ARG_WARNINGS_ARE_ERRORS, DXC_ARG_IEEE_STRICTNESS};
    UINT32 count = 9;
    if (requirements->flags & DOE_DX_SHADER_NATIVE16) arguments[count++]=L"-enable-16bit-types";
    /* Verified pinned DXC spelling. Appended signature elements must not move
     * the shared prefix; compute has no graphics signature policy. */
    if (options->stage!=DOE_DX_SHADER_COMPUTE) arguments[count++]=L"-pack-prefix-stable";
    wchar_t selected[32],projected[64];
    if (projection) {
        static_assert(DOE_COMPILER_HLSL_INTERSTAGE_MASK_BITS==32u,"projection mask width matches compiler");
        char encoded[64];
        int length=std::snprintf(encoded,sizeof(encoded),"%s=%uu",
            DOE_COMPILER_HLSL_INTERSTAGE_MASK_DEFINE,static_cast<unsigned>(projection->interstage_mask));
        if (length<=0 || static_cast<size_t>(length)>=sizeof(encoded))
            return failed(&state,error,DOE_DX_RANGE,static_cast<int32_t>(E_INVALIDARG),"DXC.projection_define");
        for (size_t i=0;i<=static_cast<size_t>(length);++i)
            projected[i]=static_cast<wchar_t>(static_cast<unsigned char>(encoded[i]));
        arguments[count++]=L"-D";
        arguments[count++]=projected;
    }
    if (options->has_entry_index) {
        int length = std::swprintf(selected, sizeof(selected) / sizeof(*selected),
            L"DOE_HLSL_ENTRY=%u", static_cast<unsigned>(options->entry_index));
        if (length <= 0 || static_cast<size_t>(length) >= sizeof(selected) / sizeof(*selected))
            return failed(&state, error, DOE_DX_RANGE, static_cast<int32_t>(E_INVALIDARG), "DXC.entry_index");
        arguments[count++] = L"-D";
        arguments[count++] = selected;
    }
    DxcBuffer source = {options->source, options->source_size, DXC_CP_UTF8};
    result = state.compiler->Compile(&source, arguments, count, nullptr, __extension__ IID_PPV_ARGS(&state.result));
    if (FAILED(result) || !state.result) return failed(&state, error,
        native_status(FAILED(result) ? result : E_FAIL),
        static_cast<int32_t>(FAILED(result) ? result : E_FAIL), "IDxcCompiler3::Compile");
    HRESULT compilation;
    result = state.result->GetStatus(&compilation);
    if (FAILED(result)) return failed(&state, error, native_status(result),
        static_cast<int32_t>(result), "IDxcResult::GetStatus");
    if (FAILED(compilation)) return failed(&state, error,
        compilation == E_OUTOFMEMORY ? DOE_DX_OUT_OF_MEMORY : DOE_DX_INVALID_ARGUMENT,
        static_cast<int32_t>(compilation), "DXC.compile");
    result = state.result->GetOutput(DXC_OUT_OBJECT, __extension__ IID_PPV_ARGS(&state.bytecode), nullptr);
    if (FAILED(result) || !state.bytecode || !state.bytecode->GetBufferSize() ||
            !state.bytecode->GetBufferPointer())
        return failed(&state, error, native_status(FAILED(result) ? result : E_FAIL),
            static_cast<int32_t>(FAILED(result) ? result : E_FAIL), "IDxcResult::GetOutput(DXC_OUT_OBJECT)");
    doe_dx_shader *shader = static_cast<doe_dx_shader *>(std::calloc(1, sizeof(*shader)));
    if (!shader) return failed(&state, error, DOE_DX_OUT_OF_MEMORY,
        static_cast<int32_t>(E_OUTOFMEMORY), "DXC.bytecode.allocate");
    shader->module = state.module;
    shader->bytecode = state.bytecode;
    state.module = nullptr;
    state.bytecode = nullptr;
    /* Compiler/result/diagnostics die before the retained object/module. */
    cleanup(&state);
    *out = shader;
    return report(error, DOE_DX_OK, 0, nullptr, nullptr, 0);
}

static doe_dx_status shader_compile(const doe_dx_shader_options *options,
    const doe_dx_shader_projection *projection,bool require_projection,
    const doe_dx_shader_requirements *requirements,
    doe_dx_shader **out,doe_dx_shader_error *error)
{
    DWORD saved_error = GetLastError();
    int saved_errno = errno;
    unsigned saved_mxcsr = _mm_getcsr();
    fenv_t saved_environment;
    doe_dx_shader_error failure = {};
    doe_dx_shader *shader = nullptr;
    doe_dx_status status;
    if (std::fegetenv(&saved_environment)) {
        status = report(&failure, DOE_DX_NATIVE_FAILURE, 0, "fegetenv(DXC)", "cannot save FP environment", 26);
    } else {
        status = compile_shader(options,projection,require_projection,requirements,out ? &shader : nullptr,&failure);
        if (std::fesetenv(&saved_environment)) {
            doe_dx_shader_release(shader);
            shader = nullptr;
            if (std::fesetenv(&saved_environment)) std::abort();
            if (status == DOE_DX_OK) status = report(&failure, DOE_DX_NATIVE_FAILURE, 0,
                "fesetenv(DXC)", "cannot restore FP environment", 29);
        }
    }
    if (status == DOE_DX_OK) *out = shader;
    if (error) *error = failure;
    _mm_setcsr(saved_mxcsr);
    errno = saved_errno;
    SetLastError(saved_error);
    return status;
}

doe_dx_status doe_dx_shader_compile(const doe_dx_shader_options *options,
    doe_dx_shader **out,doe_dx_shader_error *error)
{
    return shader_compile(options,nullptr,false,nullptr,out,error);
}

doe_dx_status doe_dx_shader_compile_projected(const doe_dx_shader_options *options,
    const doe_dx_shader_projection *projection,doe_dx_shader **out,doe_dx_shader_error *error)
{
    return shader_compile(options,projection,true,nullptr,out,error);
}

doe_dx_status doe_dx_shader_compile_required(const doe_dx_shader_options *options,
    const doe_dx_shader_projection *projection, const doe_dx_shader_requirements *requirements,
    doe_dx_shader **out, doe_dx_shader_error *error)
{
    /* A null required record is invalid, not a request for the legacy defaults.
     * Route rejection through the same host-state preservation owner. */
    const doe_dx_shader_requirements invalid = {};
    return shader_compile(options,projection,false,requirements ? requirements : &invalid,out,error);
}

const uint8_t *doe_dx_shader_bytes(const doe_dx_shader *shader, size_t *size)
{
    if (size) *size = shader ? (shader->fxc_bytecode ? shader->fxc_bytecode->GetBufferSize() :
        shader->bytecode->GetBufferSize()) : 0;
    return shader ? static_cast<const uint8_t *>(shader->fxc_bytecode ?
        shader->fxc_bytecode->GetBufferPointer() : shader->bytecode->GetBufferPointer()) : nullptr;
}

void doe_dx_shader_release(doe_dx_shader *shader)
{
    if (!shader) return;
    const DWORD saved_error = GetLastError();
    const int saved_errno = errno;
    const unsigned saved_mxcsr = _mm_getcsr();
    fenv_t saved_environment;
    if (std::fegetenv(&saved_environment)) std::abort();
    if (shader->bytecode) shader->bytecode->Release();
    if (shader->fxc_bytecode) shader->fxc_bytecode->Release();
    if (!FreeLibrary(shader->module)) std::abort();
    std::free(shader);
    if (std::fesetenv(&saved_environment)) std::abort();
    _mm_setcsr(saved_mxcsr);
    errno = saved_errno;
    SetLastError(saved_error);
}
