/* SPDX-License-Identifier: MIT
 * Actual official-DXC CPU projection/control lifetime; no device or GPU. */
#include "doe_d3d12_shader.h"
#include "../../../compiler/include/doe_compiler_hlsl.h"
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

static doe_dx_status compile(const doe_dx_shader_options *options,
    const doe_dx_shader_projection *projection,bool projected,doe_dx_shader **shader,doe_dx_shader_error *error)
{
    fenv_t original;
    assert(fegetenv(&original)==0);
    unsigned original_mxcsr=_mm_getcsr();
    assert(fesetround(FE_DOWNWARD)==0 && feclearexcept(FE_ALL_EXCEPT)==0);
    assert(feraiseexcept(FE_INVALID|FE_DIVBYZERO)==0);
    _mm_setcsr(_mm_getcsr()|0x8040u);
    unsigned expected_mxcsr=_mm_getcsr();
    int expected_flags=fetestexcept(FE_ALL_EXCEPT);
    errno=EDOM;
    SetLastError(ERROR_INVALID_DATA);
    doe_dx_status status=projected ? doe_dx_shader_compile_projected(options,projection,shader,error) :
        doe_dx_shader_compile(options,shader,error);
    DWORD native_error=GetLastError();
    assert(errno==EDOM && native_error==ERROR_INVALID_DATA && _mm_getcsr()==expected_mxcsr &&
        fegetround()==FE_DOWNWARD && fetestexcept(FE_ALL_EXCEPT)==expected_flags);
    assert(fesetenv(&original)==0);
    _mm_setcsr(original_mxcsr);
    return status;
}
static void rejected(const doe_dx_shader_options *options,const doe_dx_shader_projection *projection,
    const char *operation)
{
    doe_dx_shader_error error={0};
    doe_dx_shader *sentinel=(doe_dx_shader *)(void *)&error,*shader=sentinel;
    assert(compile(options,projection,true,&shader,&error)==DOE_DX_INVALID_ARGUMENT);
    assert(shader==sentinel && error.status==DOE_DX_INVALID_ARGUMENT && error.operation &&
        !strcmp(error.operation,operation) && error.message_size && error.message[error.message_size]==0);
}
int main(int argc,char **argv)
{
    assert(argc==2);
    _Static_assert(DOE_COMPILER_HLSL_INTERSTAGE_MASK_BITS==32u,"compiler mask width");
    static const char legacy[]=
        "#ifdef " DOE_COMPILER_HLSL_INTERSTAGE_MASK_DEFINE "\n#error legacy API supplied a projection\n#endif\n"
        "float4 selected(uint id:SV_VertexID):SV_Position{return float4(float(id),0,0,1);}\n";
    doe_dx_shader_options options={.compiler_module_path=argv[1],.source=legacy,.source_size=sizeof(legacy)-1,
        .entry="selected",.stage=DOE_DX_SHADER_VERTEX,.has_entry_index=true,.entry_index=7};
    doe_dx_shader *shader=NULL;
    assert(compile(&options,NULL,false,&shader,NULL)==DOE_DX_OK && shader);
    doe_dx_shader_release(shader);
    const uint32_t masks[]={0,UINT32_MAX,UINT32_C(0x81)};
    for (size_t i=0;i<sizeof(masks)/sizeof(*masks);++i) {
        char source[1024];
        int size=snprintf(source,sizeof(source),
            "#if !defined(%s)\n#error missing compiler-owned mask\n#endif\n"
            "#if %s != %uu\n#error wrong compiler-owned mask\n#endif\n"
            "#if DOE_HLSL_ENTRY != 7\n#error wrong compiler-owned entry\n#endif\n"
            "float4 selected(uint id:SV_VertexID):SV_Position{return float4(float(id),0,0,1);}\n",
            DOE_COMPILER_HLSL_INTERSTAGE_MASK_DEFINE,DOE_COMPILER_HLSL_INTERSTAGE_MASK_DEFINE,(unsigned)masks[i]);
        assert(size>0 && (size_t)size<sizeof(source));
        options.source=source; options.source_size=(size_t)size;
        doe_dx_shader_projection projection={.interstage_mask=masks[i]};
        shader=NULL;
        doe_dx_shader_error error={0};
        assert(compile(&options,&projection,true,&shader,&error)==DOE_DX_OK && shader && !error.message_size);
        size_t bytes=0;
        const uint8_t *code=doe_dx_shader_bytes(shader,&bytes);
        assert(code && bytes>32 && !memcmp(code,"DXBC",4));
        doe_dx_shader_release(shader);
        projection.reserved=1;
        rejected(&options,&projection,"DXC.options");
        projection.reserved=0;
        options.stage=DOE_DX_SHADER_FRAGMENT;
        rejected(&options,&projection,"DXC.options");
        options.stage=DOE_DX_SHADER_COMPUTE;
        rejected(&options,&projection,"DXC.options");
        options.stage=DOE_DX_SHADER_VERTEX;
        rejected(&options,NULL,"DXC.options");
        options.entry_index=8;
        rejected(&options,&projection,"DXC.compile");
        options.entry_index=7;
    }
    puts("PASS official DXC CPU: additive VS projection zero/full/sparse masks plus exact entry, legacy absence, wrong-stage/reserved/null rejection, untouched outputs, retained bytecode/module and FP/errno/LastError preservation; no device/GPU");
    return 0;
}
