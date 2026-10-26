/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include <stdlib.h>
#include <string.h>

static doe_shader_status import_error(doe_shader_error *error,doe_shader_status status,const char *message)
{
    if (error && !error->diagnostic.message[0]) {
        error->status=status;
        error->compiler_status=DOE_COMPILER_INPUT_ERROR;
        error->diagnostic.status=DOE_COMPILER_INPUT_ERROR;
        size_t length=strlen(message);
        if (length>=sizeof(error->diagnostic.message)) length=sizeof(error->diagnostic.message)-1;
        memcpy(error->diagnostic.message,message,length);
        error->diagnostic.message[length]=0;
    }
    return status;
}
static bool reject(native_pipeline_error *error,const char *message)
{
    error->type=WGPUErrorType_Validation;
    native_failure failure=native_failure_status(NATIVE_INVALID_ARGUMENT);
    native_failure_format(&failure,message,error->message);
    return false;
}
static bool spirv_framed(const uint32_t *words,size_t count)
{
    if (!words || count<5 || words[0]!=UINT32_C(0x07230203) ||
        words[1]>UINT32_C(0x00010300) || words[1]<UINT32_C(0x00010000) || !words[3] || words[4]) return false;
    for (size_t at=5;at<count;) {
        uint32_t length=words[at]>>16;
        if (!length || length>count-at) return false;
        at+=length;
    }
    return true;
}
doe_shader_status native_shader_import_create(native_device *device,const WGPUChainedStruct *chain,
    native_shader_import *out,doe_shader_error *error)
{
    if (!device || !chain || chain->next || !out)
        return import_error(error,DOE_SHADER_INVALID_ARGUMENT,"native shader import requires one source chain");
    native_shader_import result={0};
    const void *source=NULL;
    if (chain->sType==WGPUSType_ShaderSourceSPIRV) {
        if (device->backend.kind!=WGPUBackendType_Vulkan)
            return import_error(error,DOE_SHADER_INVALID_ARGUMENT,"SPIR-V import requires the Vulkan backend");
        const WGPUShaderSourceSPIRV *descriptor=(const WGPUShaderSourceSPIRV *)chain;
        if (!descriptor->code || !descriptor->codeSize || (uint64_t)descriptor->codeSize*4u>SIZE_MAX)
            return import_error(error,DOE_SHADER_INVALID_ARGUMENT,"SPIR-V import requires a nonempty word span");
        result.target=DOE_SHADER_SPIRV;
        result.size=(size_t)descriptor->codeSize*sizeof(uint32_t);
        source=descriptor->code;
    } else {
        WGPUStringView code;
        if (chain->sType==DOE_NATIVE_STYPE_SHADER_SOURCE_MSL) {
            if (device->backend.kind!=WGPUBackendType_Metal)
                return import_error(error,DOE_SHADER_INVALID_ARGUMENT,"MSL import requires the Metal backend");
            const DoeNativeShaderSourceMSL *descriptor=(const DoeNativeShaderSourceMSL *)chain;
            result.target=DOE_SHADER_METAL; code=descriptor->code;
            result.workgroup_size[0]=descriptor->workgroup_size_x;
            result.workgroup_size[1]=descriptor->workgroup_size_y;
            result.workgroup_size[2]=descriptor->workgroup_size_z;
        } else if (chain->sType==DOE_NATIVE_STYPE_SHADER_SOURCE_HLSL) {
            if (device->backend.kind!=WGPUBackendType_D3D12)
                return import_error(error,DOE_SHADER_INVALID_ARGUMENT,"HLSL import requires the D3D12 backend");
            const DoeNativeShaderSourceHLSL *descriptor=(const DoeNativeShaderSourceHLSL *)chain;
            result.target=DOE_SHADER_HLSL; code=descriptor->code;
            result.workgroup_size[0]=descriptor->workgroup_size_x;
            result.workgroup_size[1]=descriptor->workgroup_size_y;
            result.workgroup_size[2]=descriptor->workgroup_size_z;
        } else return import_error(error,DOE_SHADER_INVALID_ARGUMENT,"unknown native shader source chain");
        if (!native_string_size(code,&result.size) || !code.data || !result.size ||
            memchr(code.data,0,result.size))
            return import_error(error,DOE_SHADER_INVALID_ARGUMENT,"native shader text requires a nonempty exact source span");
        source=code.data;
        for (size_t i=0;i<3;++i) if (!result.workgroup_size[i]) result.workgroup_size[i]=1;
    }
    result.data=native_allocate(NULL,NULL,result.size,_Alignof(uint32_t));
    if (!result.data) return import_error(error,DOE_SHADER_OUT_OF_MEMORY,"native shader source allocation failed");
    memcpy(result.data,source,result.size);
    if (result.target==DOE_SHADER_SPIRV && !spirv_framed(result.data,result.size/sizeof(uint32_t))) {
        native_shader_import_dispose(&result);
        return import_error(error,DOE_SHADER_INVALID_ARGUMENT,"native SPIR-V header or instruction framing is invalid");
    }
    if (result.target==DOE_SHADER_METAL) {
        doe_mtl_error detail={0};
        native_lock(device);
        doe_mtl_status status=doe_mtl_shader_validate(device->backend.value.metal.context,result.data,result.size,&detail);
        native_unlock(device);
        if (status!=DOE_MTL_OK) {
            const native_failure failure=native_failure_metal(status,&detail);
            char message[NATIVE_FAILURE_MESSAGE_CAPACITY];
            native_failure_format(&failure,"native MSL source compilation failed",message);
            native_shader_import_dispose(&result);
            return import_error(error,status==DOE_MTL_OUT_OF_MEMORY ? DOE_SHADER_OUT_OF_MEMORY : DOE_SHADER_COMPILE_FAILED,message);
        }
    }
    *out=result;
    return DOE_SHADER_OK;
}
void native_shader_import_dispose(native_shader_import *imported)
{
    if (!imported) return;
    native_deallocate(NULL,NULL,imported->data,imported->size,_Alignof(uint32_t));
    memset(imported,0,sizeof(*imported));
}
static bool packed_name(const uint32_t *words,size_t count,size_t *length)
{
    for (size_t i=0;i<count*4u;++i) {
        if (!(unsigned char)(words[i/4u]>>((i%4u)*8u))) { *length=i; return i!=0; }
    }
    return false;
}
static bool own_name(native_stage *out,const char *name,size_t length,native_pipeline_error *error)
{
    if (length >= UINT32_MAX) return reject(error,"native entry name length is not representable");
    char *copy=malloc(length+1);
    if (!copy) {
        reject(error,"native entry name allocation failed");
        error->type=WGPUErrorType_OutOfMemory;
        return false;
    }
    memcpy(copy,name,length); copy[length]=0;
    out->owned_name=copy; out->name=copy;
    return true;
}
static bool spirv_entry(const native_shader_import *imported,WGPUStringView name,native_stage *out,
    native_pipeline_error *error)
{
    const uint32_t *words=imported->data;
    size_t count=imported->size/sizeof(*words), requested=0;
    if (!native_string_size(name,&requested) ||
        (name.data && (!requested || memchr(name.data,0,requested))))
        return reject(error,"native SPIR-V entry name is invalid");
    uint32_t entry=0;
    const uint32_t *selected=NULL;
    size_t selected_length=0;
    for (size_t at=5;at<count;at+=words[at]>>16) {
        uint32_t length=words[at]>>16, opcode=words[at]&65535u;
        if (opcode!=15 || length<4 || words[at+1]!=5u) continue;
        size_t candidate_length=0;
        const uint32_t *candidate=words+at+3;
        if (!packed_name(candidate,length-3,&candidate_length))
            return reject(error,"native SPIR-V entry name is empty or unterminated");
        if (name.data) {
            if (candidate_length!=requested) continue;
            bool matches=true;
            for (size_t i=0;i<requested;++i)
                if ((unsigned char)(candidate[i/4u]>>((i%4u)*8u)) != (unsigned char)name.data[i]) matches=false;
            if (!matches) continue;
        }
        if (entry) return reject(error,"native SPIR-V compute entry selection is ambiguous");
        entry=words[at+2];
        if (!entry || entry>=words[3]) return reject(error,"native SPIR-V entry identity is invalid");
        selected=candidate; selected_length=candidate_length;
    }
    if (!entry) return reject(error,"native SPIR-V compute entry was not found");
    bool geometry=false;
    for (size_t at=5;at<count;at+=words[at]>>16) {
        uint32_t length=words[at]>>16, opcode=words[at]&65535u;
        if ((opcode!=16 && opcode!=331) || length<3 || words[at+1]!=entry) continue;
        if (opcode==331 && words[at+2]==38u)
            return reject(error,"native SPIR-V LocalSizeId requires an explicit native specialization contract");
        if (opcode!=16 || words[at+2]!=17u) continue;
        if (geometry || length!=6 || !words[at+3] || !words[at+4] || !words[at+5])
            return reject(error,"native SPIR-V LocalSize is invalid or repeated");
        memcpy(out->imported_entry.workgroup_size,words+at+3,3*sizeof(uint32_t));
        geometry=true;
    }
    if (!geometry) return reject(error,"native SPIR-V compute entry has no fixed LocalSize");
    char *copy=malloc(selected_length+1);
    if (!copy) { reject(error,"native entry name allocation failed"); error->type=WGPUErrorType_OutOfMemory; return false; }
    for (size_t i=0;i<selected_length;++i) copy[i]=(char)(selected[i/4u]>>((i%4u)*8u));
    copy[selected_length]=0; out->owned_name=copy; out->name=copy;
    return true;
}
bool native_shader_import_select(native_device *device,const native_shader_import *imported,WGPUStringView name,
    uint32_t stage,native_stage *out,native_pipeline_error *error)
{
    if (!device || !imported || !imported->data || !out || !error) return false;
    out->imported=imported;
    out->imported_entry.stage=stage;
    out->view.target=imported->target;
    out->view.metal_sample_mask=UINT32_MAX;
    if (imported->target==DOE_SHADER_SPIRV) {
        if (stage!=DOE_COMPILER_STAGE_COMPUTE)
            return reject(error,"retained native SPIR-V imports expose compute entries only");
        if (!spirv_entry(imported,name,out,error)) return false;
        out->view.words=imported->data;
        out->view.counts.words=imported->size/sizeof(uint32_t);
    } else {
        size_t length=0;
        if (!name.data) { name.data="main"; name.length=4; }
        if (!native_string_size(name,&length) || !length || length>=UINT32_MAX || memchr(name.data,0,length))
            return reject(error,"native text entry name is invalid");
        if (!own_name(out,name.data,length,error)) return false;
        memcpy(out->imported_entry.workgroup_size,imported->workgroup_size,sizeof(imported->workgroup_size));
        out->view.text=imported->data; out->view.text_size=imported->size;
        out->view.target_language_version=imported->target==DOE_SHADER_METAL ? 300 : 600;
    }
    /* This is a native entry selection record, not compiler reflection. The
     * explicit imported marker and false completeness bits remain authoritative. */
    out->imported_entry.name=(DoeCompilerNameV4){0,(uint32_t)strlen(out->name)};
    out->entry=&out->imported_entry;
    out->view.entries=&out->imported_entry;
    out->view.counts.entries=1;
    out->view.names=out->name;
    out->view.counts.name_bytes=strlen(out->name)+1;
    out->visibility=stage==DOE_COMPILER_STAGE_COMPUTE ? WGPUShaderStage_Compute :
        stage==DOE_COMPILER_STAGE_VERTEX ? WGPUShaderStage_Vertex : WGPUShaderStage_Fragment;
    if (imported->target==DOE_SHADER_SPIRV && !native_shader_capabilities(&out->view,&device->backend.value.vulkan.caps))
        return reject(error,"native SPIR-V capabilities are not enabled by this backend");
    return true;
}
