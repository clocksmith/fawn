/* SPDX-License-Identifier: MIT */
#include "windows_cache.h"
#ifdef _WIN32
#include "../../../common/include/doe_sha256.h"
#include <stdio.h>
#include <wchar.h>

enum { CACHE_HEADER=96, CACHE_MAX_BLOB=32*1024*1024, CACHE_MAX_PATH=32760 };
struct doe_dx_pipeline_cache {
    ID3D12PipelineLibrary *library;
    void *input_blob; /* CreatePipelineLibrary borrows this until library Release. */
    WCHAR *path;
    uint8_t identity[32];
    uint64_t temporary_sequence;
    doe_dx_pipeline_cache_stats stats;
};
static void tick(uint64_t *value) { if (*value!=UINT64_MAX) ++*value; }
static void put32(uint8_t *p,uint32_t value) { for (unsigned i=0;i<4;++i) p[i]=(uint8_t)(value>>(8u*i)); }
static void put64(uint8_t *p,uint64_t value) { for (unsigned i=0;i<8;++i) p[i]=(uint8_t)(value>>(8u*i)); }
static uint64_t get64(const uint8_t *p) {
    uint64_t value=0; for (unsigned i=0;i<8;++i) value|=(uint64_t)p[i]<<(8u*i); return value;
}
static doe_dx_status file_error(DWORD code,const char *operation,doe_dx_error *error)
{
    return doe_dx_hresult(HRESULT_FROM_WIN32(code),operation,error);
}
static void cache_destroy(struct doe_dx_pipeline_cache *cache)
{
    if (!cache) return;
    if (cache->library) ID3D12PipelineLibrary_Release(cache->library);
    free(cache->input_blob); free(cache->path); free(cache);
}
void doe_dx_cache_close(doe_dx_native_context *context)
{
    if (!context) return;
    cache_destroy(context->cache); context->cache=NULL;
}
void doe_dx_cache_adapter(doe_dx_native_context *context,IDXGIAdapter1 *adapter)
{
    LARGE_INTEGER version={0};
    HRESULT hr=IDXGIAdapter1_CheckInterfaceSupport(adapter,&IID_IDXGIDevice,&version);
    context->cache_driver_known=SUCCEEDED(hr);
    if (SUCCEEDED(hr)) context->cache_driver_version=(uint64_t)version.QuadPart;
}
static doe_dx_status cache_path(const char *directory,size_t length,const uint8_t identity[32],
    WCHAR **out,doe_dx_error *error)
{
    int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,directory,(int)length,NULL,0);
    if (count<=0) return file_error(GetLastError(),"cache.UTF8",error);
    WCHAR *input=calloc((size_t)count+1,sizeof(*input));
    if (!input) return doe_dx_fail(DOE_DX_OUT_OF_MEMORY,0,"cache.path",error);
    if (MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,directory,(int)length,input,count)!=count) {
        DWORD code=GetLastError(); free(input); return file_error(code,"cache.UTF8",error);
    }
    DWORD needed=GetFullPathNameW(input,0,NULL,NULL);
    if (!needed || needed>CACHE_MAX_PATH-100) {
        DWORD code=needed ? ERROR_FILENAME_EXCED_RANGE : GetLastError();
        free(input); return file_error(code,"cache.absolute_path",error);
    }
    WCHAR *path=calloc((size_t)needed+100,sizeof(*path));
    if (!path) { free(input); return doe_dx_fail(DOE_DX_OUT_OF_MEMORY,0,"cache.path",error); }
    DWORD written=GetFullPathNameW(input,needed,path,NULL); free(input);
    if (!written || written>=needed) {
        DWORD code=written ? ERROR_INSUFFICIENT_BUFFER : GetLastError();
        free(path); return file_error(code,"cache.absolute_path",error);
    }
    if (!CreateDirectoryW(path,NULL) && GetLastError()!=ERROR_ALREADY_EXISTS) {
        DWORD code=GetLastError(); free(path); return file_error(code,"cache.directory",error);
    }
    DWORD attributes=GetFileAttributesW(path);
    if (attributes==INVALID_FILE_ATTRIBUTES || !(attributes&FILE_ATTRIBUTE_DIRECTORY)) {
        DWORD code=attributes==INVALID_FILE_ATTRIBUTES ? GetLastError() : ERROR_DIRECTORY;
        free(path); return file_error(code,"cache.directory",error);
    }
    size_t cursor=written;
    if (cursor && path[cursor-1]!=L'\\' && path[cursor-1]!=L'/') path[cursor++]=L'\\';
    static const WCHAR prefix[]=L"doe-d3d12-v1-";
    memcpy(path+cursor,prefix,sizeof(prefix)-sizeof(WCHAR)); cursor+=sizeof(prefix)/sizeof(WCHAR)-1;
    uint8_t digest[32]; char hex[65];
    doe_common_sha256(identity,32,digest); doe_common_sha256_hex(digest,hex);
    for (size_t i=0;i<64;++i) path[cursor++]=(WCHAR)(unsigned char)hex[i];
    memcpy(path+cursor,L".bin",5*sizeof(WCHAR)); *out=path; return DOE_DX_OK;
}
static void cache_header(uint8_t header[CACHE_HEADER],const uint8_t identity[32],
    const void *payload,size_t size)
{
    memset(header,0,CACHE_HEADER); memcpy(header,"DOEDXPL1",8);
    put32(header+8,1); put32(header+12,CACHE_HEADER); memcpy(header+16,identity,32);
    put64(header+48,size); doe_common_sha256(payload,size,header+56); put32(header+88,1);
}
/* A missing/incompatible/checksum-invalid file is not handed to the driver.
 * Real I/O failures remain errors, rather than being hidden as successful loads. */
static doe_dx_status cache_read(struct doe_dx_pipeline_cache *cache,void **bytes,size_t *size,doe_dx_error *error)
{
    *bytes=NULL; *size=0;
    HANDLE file=CreateFileW(cache->path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if (file==INVALID_HANDLE_VALUE) {
        DWORD code=GetLastError();
        if (code==ERROR_FILE_NOT_FOUND) return DOE_DX_OK;
        return file_error(code,"cache.open",error);
    }
    LARGE_INTEGER length={0};
    if (!GetFileSizeEx(file,&length)) {
        DWORD code=GetLastError(); CloseHandle(file); return file_error(code,"cache.size",error);
    }
    if (length.QuadPart<CACHE_HEADER || length.QuadPart>CACHE_HEADER+CACHE_MAX_BLOB) {
        CloseHandle(file); return DOE_DX_OK;
    }
    size_t total=(size_t)length.QuadPart;
    uint8_t *data=malloc(total);
    if (!data) { CloseHandle(file); return doe_dx_fail(DOE_DX_OUT_OF_MEMORY,0,"cache.read",error); }
    DWORD read=0;
    BOOL read_ok=ReadFile(file,data,(DWORD)total,&read,NULL);
    if (!read_ok || read!=total) {
        DWORD code=!read_ok ? GetLastError() : ERROR_HANDLE_EOF;
        CloseHandle(file); free(data); return file_error(code,"cache.read",error);
    }
    if (!CloseHandle(file)) { DWORD code=GetLastError(); free(data); return file_error(code,"cache.close",error); }
    uint8_t expected[CACHE_HEADER];
    cache_header(expected,cache->identity,data+CACHE_HEADER,total-CACHE_HEADER);
    if (memcmp(data,expected,CACHE_HEADER) || get64(data+48)!=total-CACHE_HEADER) { free(data); return DOE_DX_OK; }
    *bytes=data; *size=total-CACHE_HEADER; return DOE_DX_OK;
}
doe_dx_status doe_dx_native_cache_configure(doe_dx_native_context *context,const doe_dx_caps *caps,
    const char *directory,size_t length,doe_dx_error *error)
{
    if (!directory) { doe_dx_cache_close(context); return doe_dx_fail(DOE_DX_OK,0,"cache.disabled",error); }
    if (!context->cache_driver_known) return doe_dx_fail(DOE_DX_UNSUPPORTED,0,"cache.driver_identity",error);
    D3D12_FEATURE_DATA_SHADER_CACHE support={0};
    HRESULT hr=ID3D12Device_CheckFeatureSupport(context->device,D3D12_FEATURE_SHADER_CACHE,&support,sizeof(support));
    if (FAILED(hr)) return doe_dx_hresult(hr,"cache.shader_support",error);
    if (!(support.SupportFlags&D3D12_SHADER_CACHE_SUPPORT_LIBRARY))
        return doe_dx_fail(DOE_DX_UNSUPPORTED,0,"cache.pipeline_library_support",error);
    ID3D12Device1 *device=NULL;
    hr=ID3D12Device_QueryInterface(context->device,&IID_ID3D12Device1,(void **)&device);
    if (FAILED(hr)) return doe_dx_hresult(hr,"cache.device1",error);
    struct doe_dx_pipeline_cache *cache=calloc(1,sizeof(*cache));
    if (!cache) { ID3D12Device1_Release(device); return doe_dx_fail(DOE_DX_OUT_OF_MEMORY,0,"cache.allocate",error); }
    put32(cache->identity,caps->vendor_id); put32(cache->identity+4,caps->device_id);
    put32(cache->identity+8,caps->luid_low); put32(cache->identity+12,(uint32_t)caps->luid_high);
    put64(cache->identity+16,context->cache_driver_version); put32(cache->identity+24,caps->software_adapter ? 1u : 0u);
    doe_dx_status status=cache_path(directory,length,cache->identity,&cache->path,error);
    size_t payload_size=0;
    if (status==DOE_DX_OK) { tick(&cache->stats.load_attempts); status=cache_read(cache,&cache->input_blob,&payload_size,error); }
    if (status==DOE_DX_OK) {
        const void *payload=cache->input_blob ? (uint8_t *)cache->input_blob+CACHE_HEADER : NULL;
        hr=ID3D12Device1_CreatePipelineLibrary(device,payload,payload_size,&IID_ID3D12PipelineLibrary,(void **)&cache->library);
        if (FAILED(hr) && payload && (hr==E_INVALIDARG || hr==D3D12_ERROR_DRIVER_VERSION_MISMATCH || hr==D3D12_ERROR_ADAPTER_NOT_FOUND)) {
            /* Documented serialization incompatibility, not a failed PSO retry. */
            if (cache->library) { ID3D12PipelineLibrary_Release(cache->library); cache->library=NULL; }
            free(cache->input_blob); cache->input_blob=NULL;
            hr=ID3D12Device1_CreatePipelineLibrary(device,NULL,0,&IID_ID3D12PipelineLibrary,(void **)&cache->library);
        }
        if (FAILED(hr)) status=doe_dx_hresult(hr,"ID3D12Device1.CreatePipelineLibrary",error);
        else if (cache->input_blob) tick(&cache->stats.load_successes);
    }
    ID3D12Device1_Release(device);
    if (status!=DOE_DX_OK) { cache_destroy(cache); return status; }
    cache->stats.enabled=true;
    struct doe_dx_pipeline_cache *old=context->cache; context->cache=cache; cache_destroy(old);
    return doe_dx_fail(DOE_DX_OK,0,"cache.configured",error);
}
static doe_dx_status cache_write(struct doe_dx_pipeline_cache *cache,const void *data,size_t size,doe_dx_error *error)
{
    size_t length=wcslen(cache->path);
    WCHAR *temporary=calloc(length+80,sizeof(*temporary));
    if (!temporary) return doe_dx_fail(DOE_DX_OUT_OF_MEMORY,0,"cache.temporary_path",error);
    HANDLE file=INVALID_HANDLE_VALUE;
    for (unsigned attempt=0;attempt<8;++attempt) {
        if (cache->temporary_sequence==UINT64_MAX) { free(temporary); return doe_dx_fail(DOE_DX_RANGE,0,"cache.temporary_sequence",error); }
        ++cache->temporary_sequence;
        int count=swprintf_s(temporary,length+80,L"%ls.%lu.%llu.%llu.tmp",cache->path,(unsigned long)GetCurrentProcessId(),
            (unsigned long long)GetTickCount64(),(unsigned long long)cache->temporary_sequence);
        if (count<0) { free(temporary); return doe_dx_fail(DOE_DX_RANGE,0,"cache.temporary_path",error); }
        file=CreateFileW(temporary,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
        if (file!=INVALID_HANDLE_VALUE) break;
        DWORD code=GetLastError();
        if (code!=ERROR_FILE_EXISTS && code!=ERROR_ALREADY_EXISTS) { free(temporary); return file_error(code,"cache.create_temporary",error); }
    }
    if (file==INVALID_HANDLE_VALUE) { free(temporary); return file_error(ERROR_FILE_EXISTS,"cache.create_temporary",error); }
    DWORD written=0,code=0;
    if (!WriteFile(file,data,(DWORD)size,&written,NULL)) code=GetLastError();
    else if (written!=size) code=ERROR_WRITE_FAULT;
    if (!code && !FlushFileBuffers(file)) code=GetLastError();
    if (!CloseHandle(file) && !code) code=GetLastError();
    if (!code && !MoveFileExW(temporary,cache->path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) code=GetLastError();
    if (code) DeleteFileW(temporary); /* This invocation exclusively created it. */
    free(temporary);
    return code ? file_error(code,"cache.atomic_write",error) : DOE_DX_OK;
}
doe_dx_status doe_dx_native_cache_flush(doe_dx_native_context *context,doe_dx_error *error)
{
    struct doe_dx_pipeline_cache *cache=context->cache;
    if (!cache || !cache->stats.dirty) return doe_dx_fail(DOE_DX_OK,0,"cache.clean",error);
    SIZE_T size=ID3D12PipelineLibrary_GetSerializedSize(cache->library);
    if (!size || size>CACHE_MAX_BLOB) {
        tick(&cache->stats.serialization_failures); return doe_dx_fail(DOE_DX_RANGE,0,"cache.serialized_size",error);
    }
    uint8_t *bytes=malloc(CACHE_HEADER+size);
    if (!bytes) { tick(&cache->stats.serialization_failures); return doe_dx_fail(DOE_DX_OUT_OF_MEMORY,0,"cache.serialize",error); }
    HRESULT hr=ID3D12PipelineLibrary_Serialize(cache->library,bytes+CACHE_HEADER,size);
    doe_dx_status status=DOE_DX_OK;
    if (FAILED(hr)) status=doe_dx_hresult(hr,"ID3D12PipelineLibrary.Serialize",error);
    else { cache_header(bytes,cache->identity,bytes+CACHE_HEADER,size); status=cache_write(cache,bytes,CACHE_HEADER+size,error); }
    free(bytes);
    if (status==DOE_DX_OK) { cache->stats.dirty=false; tick(&cache->stats.serializations); }
    else tick(&cache->stats.serialization_failures);
    return status;
}
void doe_dx_native_cache_stats(const doe_dx_native_context *context,doe_dx_pipeline_cache_stats *out)
{
    *out=context->cache ? context->cache->stats : (doe_dx_pipeline_cache_stats){0};
}
static doe_dx_status cache_store(struct doe_dx_pipeline_cache *cache,const WCHAR *name,
    ID3D12PipelineState *pipeline,doe_dx_error *error)
{
    HRESULT hr=ID3D12PipelineLibrary_StorePipeline(cache->library,name,pipeline);
    if (FAILED(hr)) {
        tick(&cache->stats.addition_failures);
        doe_dx_error detail={0};
        doe_dx_status status=doe_dx_hresult(hr,"ID3D12PipelineLibrary.StorePipeline",&detail);
        /* A cache-store failure cannot invalidate a successfully created PSO.
         * It is observable in cache counters; genuine device loss still enters
         * the one context loss/retirement owner through the caller. */
        if (status==DOE_DX_DEVICE_LOST) { if (error) *error=detail; return status; }
        return doe_dx_fail(DOE_DX_OK,0,"cache.store_failed_after_valid_PSO",error);
    }
    tick(&cache->stats.additions); cache->stats.dirty=true; return DOE_DX_OK;
}
doe_dx_status doe_dx_cached_compute(doe_dx_native_context *context,const D3D12_COMPUTE_PIPELINE_STATE_DESC *desc,
    const uint8_t root[32],ID3D12PipelineState **out,doe_dx_error *error)
{
    ID3D12PipelineState *pipeline=NULL; struct doe_dx_pipeline_cache *cache=context->cache;
    WCHAR name[65]; HRESULT hr=E_INVALIDARG;
    if (cache) {
        if (!doe_dx_cache_compute_key(desc,root,name)) return doe_dx_fail(DOE_DX_RANGE,0,"cache.compute_key",error);
        hr=ID3D12PipelineLibrary_LoadComputePipeline(cache->library,name,desc,&IID_ID3D12PipelineState,(void **)&pipeline);
        if (SUCCEEDED(hr)) { *out=pipeline; return DOE_DX_OK; }
        if (pipeline) { ID3D12PipelineState_Release(pipeline); pipeline=NULL; }
        if (hr!=E_INVALIDARG) return doe_dx_hresult(hr,"ID3D12PipelineLibrary.LoadComputePipeline",error);
    }
    hr=ID3D12Device_CreateComputePipelineState(context->device,desc,&IID_ID3D12PipelineState,(void **)&pipeline);
    if (FAILED(hr)) {
        if (pipeline) ID3D12PipelineState_Release(pipeline);
        return doe_dx_hresult(hr,"ID3D12Device.CreateComputePipelineState",error);
    }
    doe_dx_status status=cache ? cache_store(cache,name,pipeline,error) : DOE_DX_OK;
    if (status!=DOE_DX_OK) { ID3D12PipelineState_Release(pipeline); return status; }
    *out=pipeline; return DOE_DX_OK;
}
doe_dx_status doe_dx_cached_graphics(doe_dx_native_context *context,const D3D12_GRAPHICS_PIPELINE_STATE_DESC *desc,
    const uint8_t root[32],ID3D12PipelineState **out,doe_dx_error *error)
{
    ID3D12PipelineState *pipeline=NULL; struct doe_dx_pipeline_cache *cache=context->cache;
    WCHAR name[65]; HRESULT hr=E_INVALIDARG;
    if (cache) {
        if (!doe_dx_cache_graphics_key(desc,root,name)) return doe_dx_fail(DOE_DX_RANGE,0,"cache.graphics_key",error);
        hr=ID3D12PipelineLibrary_LoadGraphicsPipeline(cache->library,name,desc,&IID_ID3D12PipelineState,(void **)&pipeline);
        if (SUCCEEDED(hr)) { *out=pipeline; return DOE_DX_OK; }
        if (pipeline) { ID3D12PipelineState_Release(pipeline); pipeline=NULL; }
        if (hr!=E_INVALIDARG) return doe_dx_hresult(hr,"ID3D12PipelineLibrary.LoadGraphicsPipeline",error);
    }
    hr=ID3D12Device_CreateGraphicsPipelineState(context->device,desc,&IID_ID3D12PipelineState,(void **)&pipeline);
    if (FAILED(hr)) {
        if (pipeline) ID3D12PipelineState_Release(pipeline);
        return doe_dx_hresult(hr,"ID3D12Device.CreateGraphicsPipelineState",error);
    }
    doe_dx_status status=cache ? cache_store(cache,name,pipeline,error) : DOE_DX_OK;
    if (status!=DOE_DX_OK) { ID3D12PipelineState_Release(pipeline); return status; }
    *out=pipeline; return DOE_DX_OK;
}
#endif
