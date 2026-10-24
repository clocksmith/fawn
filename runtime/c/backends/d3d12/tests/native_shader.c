/* SPDX-License-Identifier: MIT
 * Actual official-DXC API CPU control; no device, shader execution or GPU. */
#include "doe_d3d12_shader.h"
#ifndef _WIN32
#error This control requires the actual Windows loader and official DXC module.
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <assert.h>
#include <errno.h>
#include <fenv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xmmintrin.h>

static const char compute[] =
    "RWStructuredBuffer<uint> output : register(u0);\n"
    "[numthreads(1,1,1)] void main(uint3 tid : SV_DispatchThreadID) { output[tid.x] = 42u; }\n";
static const char vertex[] =
    "float4 main(uint id : SV_VertexID) : SV_Position { return float4(float(id),0.0,0.0,1.0); }\n";
static const char fragment[] =
    "float4 main() : SV_Target0 { return float4(0.0,1.0,0.0,1.0); }\n";
static const char bundle[] =
    "#if DOE_HLSL_ENTRY == 7\n"
    "[numthreads(1,1,1)] void selected() {}\n"
    "#else\n#error wrong compiler-owned entry index\n#endif\n";

static doe_dx_status preserved_compile(const doe_dx_shader_options *options,
    doe_dx_shader **shader, doe_dx_shader_error *error)
{
    fenv_t original;
    assert(fegetenv(&original) == 0);
    unsigned original_mxcsr = _mm_getcsr();
    assert(fesetround(FE_DOWNWARD) == 0);
    assert(feclearexcept(FE_ALL_EXCEPT) == 0);
    assert(feraiseexcept(FE_INVALID | FE_DIVBYZERO) == 0);
    _mm_setcsr(_mm_getcsr() | 0x8040u); /* Actual x86 FTZ and DAZ control bits. */
    unsigned expected_mxcsr = _mm_getcsr();
    int expected_flags = fetestexcept(FE_ALL_EXCEPT);
    errno = EDOM;
    SetLastError(ERROR_INVALID_DATA);
    doe_dx_status result = doe_dx_shader_compile(options, shader, error);
    DWORD native_error = GetLastError();
    assert(errno == EDOM && native_error == ERROR_INVALID_DATA);
    assert(_mm_getcsr() == expected_mxcsr);
    assert(fegetround() == FE_DOWNWARD && fetestexcept(FE_ALL_EXCEPT) == expected_flags);
    assert(fesetenv(&original) == 0);
    _mm_setcsr(original_mxcsr);
    return result;
}

static void valid(doe_dx_shader_options *options, const char *source, doe_dx_shader_stage stage)
{
    options->source = source;
    options->source_size = strlen(source);
    options->stage = stage;
    options->entry = "main";
    options->has_entry_index = false;
    doe_dx_shader *shader = NULL;
    doe_dx_shader_error error;
    assert(preserved_compile(options, &shader, &error) == DOE_DX_OK);
    assert(shader && error.status == DOE_DX_OK && !error.message_size);
    size_t size;
    const uint8_t *bytes = doe_dx_shader_bytes(shader, &size);
    assert(bytes && size > 32 && memcmp(bytes, "DXBC", 4) == 0);
    doe_dx_shader_release(shader);
}

static doe_dx_shader_error invalid(doe_dx_shader_options *options, doe_dx_status expected,
    const char *operation, const char *needle)
{
    doe_dx_shader_error error;
    doe_dx_shader *sentinel = (doe_dx_shader *)(void *)&error;
    doe_dx_shader *shader = sentinel;
    assert(preserved_compile(options, &shader, &error) == expected);
    assert(shader == sentinel && error.status == expected);
    assert(error.operation && strcmp(error.operation, operation) == 0);
    assert(error.message_size && error.message_size < sizeof(error.message));
    assert(error.message[error.message_size] == 0);
    if (needle) assert(strstr(error.message, needle));
    return error;
}

int main(int argc, char **argv)
{
    assert(argc == 3); /* Absolute compiler DLL and an existing include file. */
    doe_dx_shader_options options = {.compiler_module_path=argv[1], .source=compute,
        .source_size=sizeof(compute)-1, .entry="main", .stage=DOE_DX_SHADER_COMPUTE};
    FILE *include = fopen(argv[2], "rb");
    assert(include && fclose(include) == 0);
    valid(&options, compute, DOE_DX_SHADER_COMPUTE);
    valid(&options, vertex, DOE_DX_SHADER_VERTEX);
    valid(&options, fragment, DOE_DX_SHADER_FRAGMENT);

    options.source = compute;
    options.source_size = sizeof(compute)-1;
    options.stage = DOE_DX_SHADER_COMPUTE;
    options.entry = "missing_entry";
    invalid(&options, DOE_DX_INVALID_ARGUMENT, "DXC.compile", "missing_entry");
    options.entry = "main";
    options.stage = DOE_DX_SHADER_VERTEX;
    invalid(&options, DOE_DX_INVALID_ARGUMENT, "DXC.compile", NULL);
    options.stage = DOE_DX_SHADER_COMPUTE;
    options.source = "[numthreads(1,1,1)] void main() { not_declared = 1; }";
    options.source_size = strlen(options.source);
    doe_dx_shader_error retained = invalid(&options, DOE_DX_INVALID_ARGUMENT, "DXC.compile", "not_declared");
    doe_dx_shader_error original = retained;
    valid(&options, compute, DOE_DX_SHADER_COMPUTE);
    assert(memcmp(&retained, &original, sizeof(retained)) == 0);

    char path[2048];
    assert(strlen(argv[2]) < sizeof(path));
    strcpy(path, argv[2]);
    for (char *c = path; *c; ++c) if (*c == '\\') *c = '/';
    char including[4096];
    int length = snprintf(including, sizeof(including), "#include \"%s\"\n%s", path, compute);
    assert(length > 0 && (size_t)length < sizeof(including));
    options.source = including;
    options.source_size = (size_t)length;
    invalid(&options, DOE_DX_INVALID_ARGUMENT, "DXC.compile", NULL);

    char long_error[4096] = "#error ";
    memset(long_error + 7, 'x', sizeof(long_error)-9);
    long_error[sizeof(long_error)-2] = '\n';
    long_error[sizeof(long_error)-1] = 0;
    options.source = long_error;
    options.source_size = sizeof(long_error)-1;
    doe_dx_shader_error bounded = invalid(&options, DOE_DX_INVALID_ARGUMENT, "DXC.compile", NULL);
    assert(bounded.truncated && bounded.message_size == sizeof(bounded.message)-1);

    options.source = bundle;
    options.source_size = sizeof(bundle)-1;
    options.entry = "selected";
    options.has_entry_index = true;
    options.entry_index = 8;
    invalid(&options, DOE_DX_INVALID_ARGUMENT, "DXC.compile", "wrong compiler-owned entry index");
    options.entry_index = 7;
    doe_dx_shader *shader = NULL;
    assert(preserved_compile(&options, &shader, NULL) == DOE_DX_OK && shader);
    doe_dx_shader_release(shader);

    options.compiler_module_path = "dxcompiler.dll";
    invalid(&options, DOE_DX_INVALID_ARGUMENT, "DXC.options", "fully qualified");
    char missing_module[4096];
    length = snprintf(missing_module, sizeof(missing_module), "%s.missing", argv[1]);
    assert(length > 0 && (size_t)length < sizeof(missing_module));
    options.compiler_module_path = missing_module;
    invalid(&options, DOE_DX_UNAVAILABLE, "LoadLibraryExW(DXC)", NULL);
    options.compiler_module_path = argv[1];
    options.stage = (doe_dx_shader_stage)99;
    invalid(&options, DOE_DX_INVALID_ARGUMENT, "DXC.options", "stage");
    options.stage = DOE_DX_SHADER_COMPUTE;
    options.entry = "main -T ps_6_0";
    invalid(&options, DOE_DX_INVALID_ARGUMENT, "DXC.options", "entry");

    /* The source and the caller's loader reference may disappear; the returned
     * blob still retains its own DXC module and immutable object bytes. */
    char *owned_source = malloc(sizeof(compute));
    assert(owned_source);
    memcpy(owned_source, compute, sizeof(compute));
    options.source = owned_source;
    options.source_size = sizeof(compute)-1;
    options.entry = "main";
    options.has_entry_index = false;
    HMODULE external = LoadLibraryA(argv[1]);
    assert(external);
    shader = NULL;
    assert(preserved_compile(&options, &shader, NULL) == DOE_DX_OK && shader);
    assert(FreeLibrary(external));
    memset(owned_source, 0xa5, sizeof(compute));
    free(owned_source);
    size_t size;
    const uint8_t *bytes = doe_dx_shader_bytes(shader, &size);
    assert(bytes && size > 32 && memcmp(bytes, "DXBC", 4) == 0);
    uint32_t checksum = 2166136261u;
    for (size_t i = 0; i < size; ++i) checksum = (checksum ^ bytes[i]) * 16777619u;
    printf("Actual DXC object: %zu bytes, content checksum %08x (not a cryptographic identity)\n", size, checksum);
    doe_dx_shader_release(shader);
    assert(doe_dx_shader_bytes(NULL, &size) == NULL && size == 0);
    doe_dx_shader_release(NULL);
    puts("PASS official DXC CPU: cs/vs/ps6.0, exact entry/index, invalid source/stage/include, owned diagnostics, untouched failure output, blob/module lifetime, errno/LastError/fenv/MXCSR preservation; no GPU");
}
