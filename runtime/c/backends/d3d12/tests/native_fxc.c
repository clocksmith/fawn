/* SPDX-License-Identifier: MIT
 * Actual official FXC SDK CPU control; no device, shader execution or GPU. */
#define COBJMACROS
#include "doe_d3d12_shader.h"
#ifndef _WIN32
#error This control requires the actual Windows SDK and selected FXC module.
#endif
#include <windows.h>
#include <d3dcompiler.h>
#include <d3d12shader.h>
#include <assert.h>
#include <errno.h>
#include <fenv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xmmintrin.h>

static doe_dx_status preserved(const doe_dx_shader_options *options,
    const doe_dx_shader_requirements *requirements,doe_dx_shader **out,doe_dx_shader_error *error)
{
    fenv_t initial;
    assert(fegetenv(&initial) == 0);
    const unsigned initial_mxcsr = _mm_getcsr();
    assert(fesetround(FE_DOWNWARD) == 0 && feclearexcept(FE_ALL_EXCEPT) == 0);
    assert(feraiseexcept(FE_INVALID | FE_DIVBYZERO) == 0);
    _mm_setcsr(_mm_getcsr() | 0x8040u);
    const unsigned expected_mxcsr = _mm_getcsr();
    const int expected_flags = fetestexcept(FE_ALL_EXCEPT);
    errno = EDOM; SetLastError(ERROR_INVALID_DATA);
    const doe_dx_status status = doe_dx_shader_compile_required(options,NULL,requirements,out,error);
    assert(errno == EDOM && GetLastError() == ERROR_INVALID_DATA && _mm_getcsr() == expected_mxcsr);
    assert(fegetround() == FE_DOWNWARD && fetestexcept(FE_ALL_EXCEPT) == expected_flags);
    assert(fesetenv(&initial) == 0); _mm_setcsr(initial_mxcsr);
    return status;
}

static void reflect(HMODULE module,const doe_dx_shader *shader)
{
    FARPROC symbol = GetProcAddress(module,"D3DReflect");
    assert(symbol);
    /* Derive this SDK's function-pointer type from its actual declaration;
     * d3dcompiler.h supplies no C pointer typedef for D3DReflect. */
    __extension__ __typeof__(&D3DReflect) reflection;
    _Static_assert(sizeof(reflection) == sizeof(symbol),"SDK procedure pointer width");
    memcpy(&reflection,&symbol,sizeof(reflection));
    size_t size = 0;
    const uint8_t *bytes = doe_dx_shader_bytes(shader,&size);
    assert(bytes && size > 32u && !memcmp(bytes,"DXBC",4u));
    ID3D12ShaderReflection *view = NULL;
    assert(SUCCEEDED(reflection(bytes,size,&IID_ID3D12ShaderReflection,(void **)&view)) && view);
    D3D12_SHADER_DESC desc;
    assert(SUCCEEDED(view->lpVtbl->GetDesc(view,&desc)));
    assert(D3D12_SHVER_GET_TYPE(desc.Version) == D3D12_SHVER_COMPUTE_SHADER);
    assert(D3D12_SHVER_GET_MAJOR(desc.Version) == 5u && D3D12_SHVER_GET_MINOR(desc.Version) == 1u);
    UINT x,y,z;
    assert(view->lpVtbl->GetThreadGroupSize(view,&x,&y,&z) != 0 && x && y && z);
    view->lpVtbl->Release(view);
}

static bool valid(doe_dx_shader_options options,HMODULE module)
{
    const doe_dx_shader_requirements requirements = {501u,0u,{0u,0u}};
    doe_dx_shader *shader = NULL;
    doe_dx_shader_error error = {0};
    const doe_dx_status status = preserved(&options,&requirements,&shader,&error);
    if (status != DOE_DX_OK) fprintf(stderr,"FXC status=%u operation=%s: %s\n",
        (unsigned)status,error.operation ? error.operation : "none",error.message);
    if (status != DOE_DX_OK) { assert(!shader); return false; }
    assert(shader);
    reflect(module,shader);
    errno = ERANGE; SetLastError(ERROR_INVALID_DATA);
    const unsigned mxcsr = _mm_getcsr();
    const int rounding = fegetround(), flags = fetestexcept(FE_ALL_EXCEPT);
    doe_dx_shader_release(shader);
    assert(errno == ERANGE && GetLastError() == ERROR_INVALID_DATA && _mm_getcsr() == mxcsr);
    assert(fegetround() == rounding && fetestexcept(FE_ALL_EXCEPT) == flags);
    return true;
}

static char *read_file(const char *path,size_t *size)
{
    FILE *file = fopen(path,"rb");
    assert(file && fseek(file,0,SEEK_END) == 0);
    const long end = ftell(file);
    assert(end > 0 && end <= 1024*1024 && fseek(file,0,SEEK_SET) == 0);
    char *bytes = malloc((size_t)end+1u);
    assert(bytes && fread(bytes,1,(size_t)end,file) == (size_t)end && fclose(file) == 0);
    bytes[end] = 0; *size = (size_t)end;
    return bytes;
}

int main(int argc,char **argv)
{
    assert(argc == 3); /* Explicit SDK FXC path and fresh certified corpus directory. */
    HMODULE module = LoadLibraryA(argv[1]);
    assert(module);
    doe_dx_shader_options options = {.compiler_module_path=argv[1],.entry="main",.stage=DOE_DX_SHADER_COMPUTE};
    static const char basic[] = "RWStructuredBuffer<uint> b:register(u0,space3);"
        "[numthreads(1,1,1)]void main(){b[0]=42u;}";
    options.source=basic; options.source_size=sizeof(basic)-1u;
    assert(valid(options,module));
    const doe_dx_shader_requirements requirements = {501u,0u,{0u,0u}};
    doe_dx_shader_error error = {0};
    doe_dx_shader *sentinel = (doe_dx_shader *)(void *)&error, *shader = sentinel;
    doe_dx_shader_requirements contradictory = {600u,0u,{0u,0u}};
    assert(preserved(&options,&contradictory,&shader,&error) == DOE_DX_UNAVAILABLE && shader == sentinel);
    assert(!strcmp(error.operation,"GetProcAddress(DxcCreateInstance)")); /* No FXC fallback. */
    contradictory = requirements; contradictory.flags=DOE_DX_SHADER_NATIVE16;
    assert(preserved(&options,&contradictory,&shader,&error) == DOE_DX_INVALID_ARGUMENT && shader == sentinel);
    contradictory = requirements; contradictory.reserved[0]=1u;
    assert(preserved(&options,&contradictory,&shader,&error) == DOE_DX_INVALID_ARGUMENT && shader == sentinel);
    const doe_dx_shader_projection projection = {0};
    assert(doe_dx_shader_compile_required(&options,&projection,&requirements,&shader,&error) == DOE_DX_INVALID_ARGUMENT);
    assert(shader == sentinel);
    options.stage = DOE_DX_SHADER_VERTEX;
    assert(preserved(&options,&requirements,&shader,&error) == DOE_DX_INVALID_ARGUMENT && shader == sentinel);
    options.stage = DOE_DX_SHADER_COMPUTE;
    options.source = "#include \"forbidden.h\"\n[numthreads(1,1,1)]void main(){}";
    options.source_size = strlen(options.source);
    assert(preserved(&options,&requirements,&shader,&error) == DOE_DX_INVALID_ARGUMENT && shader == sentinel);
    assert(error.operation && !strcmp(error.operation,"FXC.compile") && error.message_size);
    const doe_dx_shader_error retained = error;
    options.source = "#if DOE_HLSL_ENTRY != 7\n#error wrong selected entry\n#endif\n[numthreads(1,1,1)]void main(){}";
    options.source_size = strlen(options.source); options.has_entry_index=true; options.entry_index=7u;
    assert(valid(options,module));
    assert(!memcmp(&retained,&error,sizeof(error))); /* Later success owns separate diagnostics. */
    options.entry_index=8u;
    assert(preserved(&options,&requirements,&shader,&error) == DOE_DX_INVALID_ARGUMENT && shader == sentinel);
    options.compiler_module_path="d3dcompiler_47.dll";
    assert(preserved(&options,&requirements,&shader,&error) == DOE_DX_INVALID_ARGUMENT && shader == sentinel);
    options.compiler_module_path=argv[1]; options.has_entry_index=false;
    options.source=basic; options.source_size=sizeof(basic)-1u;
    char *owned = malloc(sizeof(basic));
    assert(owned); memcpy(owned,basic,sizeof(basic)); options.source=owned;
    shader=NULL;
    assert(preserved(&options,&requirements,&shader,&error) == DOE_DX_OK && shader);
    memset(owned,0xa5,sizeof(basic)); free(owned);
    assert(FreeLibrary(module)); /* The shader's independent module lease remains. */
    reflect(module,shader);
    doe_dx_shader_release(shader);
    module=LoadLibraryA(argv[1]); assert(module);
    char search[1024];
    int length = snprintf(search,sizeof(search),"%s/*.hlsl",argv[2]);
    assert(length > 0 && (size_t)length < sizeof(search));
    WIN32_FIND_DATAA row;
    HANDLE files = FindFirstFileA(search,&row);
    assert(files != INVALID_HANDLE_VALUE);
    unsigned count = 0, failures = 0;
    do {
        if (row.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        char path[1024];
        length=snprintf(path,sizeof(path),"%s/%s",argv[2],row.cFileName);
        assert(length > 0 && (size_t)length < sizeof(path));
        size_t size = 0;
        char *source = read_file(path,&size);
        assert((size_t)length >= 5u && (size_t)length+1u < sizeof(path));
        memcpy(path+length-5,".entry",7u);
        size_t entry_size = 0;
        char *entry = read_file(path,&entry_size);
        assert(entry_size < 64u);
        options.source=source; options.source_size=size; options.entry=entry;
        options.has_entry_index=true; options.entry_index=0u;
        printf("FXC corpus %s\n",row.cFileName); fflush(stdout);
        if (!valid(options,module)) ++failures;
        free(entry); free(source); ++count;
    } while (FindNextFileA(files,&row));
    assert(GetLastError() == ERROR_NO_MORE_FILES && FindClose(files));
    assert(FreeLibrary(module));
    printf("FXC corpus complete: cases=%u failures=%u\n",count,failures); fflush(stdout);
    assert(count >= 6u && failures == 0u);
    printf("PASS actual SDK FXC: %u certified emitted compute units + explicit cs5.1 reflection, strict compile, entry/index, rejected projection stage/include/path, retained typed blobs, errno/LastError/fenv/MXCSR; no GPU\n",count);
    return 0;
}
