/* SPDX-License-Identifier: MIT
 * Official SDK typed call ledger, no adapter/device creation or GPU execution. */
#include "../src/internal.h"
#include "../src/windows_internal.h"
#include <assert.h>
#include <stdio.h>
typedef struct fence_control { ID3D12Fence iface; unsigned references; } fence_control;
typedef struct device_control { ID3D12Device iface; unsigned references; HRESULT get_result; } device_control;
static fence_control source;
static device_control device;
static unsigned waits,executes;
static ULONG STDMETHODCALLTYPE fence_add(ID3D12Fence *self) { (void)self; return ++source.references; }
static ULONG STDMETHODCALLTYPE fence_release(ID3D12Fence *self) { (void)self; assert(source.references); return --source.references; }
static ULONG STDMETHODCALLTYPE device_release(ID3D12Device *self) { (void)self; assert(device.references); return --device.references; }
static HRESULT STDMETHODCALLTYPE fence_device(ID3D12Fence *self,REFIID iid,void **out)
{
    assert(self==&source.iface && IsEqualIID(iid,&IID_ID3D12Device));
    if (FAILED(device.get_result)) return device.get_result;
    ++device.references; *out=&device.iface; return S_OK;
}
static HRESULT STDMETHODCALLTYPE open_shared(ID3D12Device *self,HANDLE handle,REFIID iid,void **out)
{
    assert(self==&device.iface && handle && IsEqualIID(iid,&IID_ID3D12Fence));
    ++source.references; *out=&source.iface; return S_OK;
}
static HRESULT STDMETHODCALLTYPE queue_wait(ID3D12CommandQueue *queue,ID3D12Fence *fence,UINT64 value)
{
    (void)queue; assert(fence==&source.iface && value==waits+11);
    ++waits; return waits==2 ? E_FAIL : S_OK;
}
static void STDMETHODCALLTYPE execute(ID3D12CommandQueue *queue,UINT count,ID3D12CommandList *const *lists)
{ (void)queue; (void)count; (void)lists; ++executes; }
int main(void)
{
    ID3D12FenceVtbl fm={0}; fm.AddRef=fence_add; fm.Release=fence_release; fm.GetDevice=fence_device;
    ID3D12DeviceVtbl dm={0}; dm.Release=device_release; dm.OpenSharedHandle=open_shared;
    ID3D12CommandQueueVtbl qm={0}; qm.Wait=queue_wait; qm.ExecuteCommandLists=execute;
    source.iface.lpVtbl=&fm; device.iface.lpVtbl=&dm; device.references=1;
    ID3D12CommandQueue queue={&qm};
    doe_dx_native_context native={.device=&device.iface,.queue=&queue};
    doe_dx_context context={.native=&native};
    doe_dx_native_commands list={.context=&native};
    doe_dx_commands commands={.native=&list,.context=&context,.state=DOE_DX_RECORDING};
    HANDLE borrowed=CreateEventW(NULL,TRUE,FALSE,NULL); assert(borrowed);
    doe_dx_shared_fence *imported=NULL; doe_dx_error error={0};
    assert(doe_dx_shared_fence_import(&context,borrowed,&imported,&error)==DOE_DX_OK);
    CloseHandle(borrowed); assert(source.references==1 && context.objects==1);
    device.get_result=E_OUTOFMEMORY;
    assert(doe_dx_commands_wait_shared_fence(&commands,imported,11,&error)==DOE_DX_OUT_OF_MEMORY);
    assert(!list.shared_waits && source.references==1 && device.references==1);
    device.get_result=S_OK;
    assert(doe_dx_commands_wait_shared_fence(&commands,imported,11,&error)==DOE_DX_OK);
    assert(doe_dx_commands_wait_shared_fence(&commands,imported,12,&error)==DOE_DX_OK);
    doe_dx_shared_fence_release(imported);
    assert(source.references==2 && device.references==3 && !context.objects);
    assert(doe_dx_native_execute(&native,&list,&error)==DOE_DX_NATIVE_FAILURE);
    assert(error.native_code==(int32_t)E_FAIL && waits==2 && !executes);
    assert(source.references==2 && device.references==3); /* Failure never retires leases. */
    doe_dx_shared_waits_close(&list); /* Supplied terminal/discard precondition, not a simulated fence. */
    assert(!source.references && device.references==1 && !list.shared_waits);
    doe_dx_native_texture image={.shared=true};
    doe_dx_texture texture={.native=&image,.context=&context};
    doe_dx_commands unknown={.state=DOE_DX_SUBMITTED}; context.pending=&unknown; context.next_fence=19;
    assert(doe_dx_shared_texture_begin(&texture,true,false,NULL)==DOE_DX_BUSY && !texture.shared_active && !texture.shared_begin_fence);
    unknown.fence=19;
    assert(doe_dx_shared_texture_begin(&texture,true,false,NULL)==DOE_DX_OK && texture.shared_begin_fence==19);
    texture.owner=&unknown;
    doe_dx_shared_fence *unchanged=(doe_dx_shared_fence *)&source; uint64_t point=999; bool initialized=false;
    assert(doe_dx_shared_texture_release_point(&texture,&unchanged,&point,&initialized,NULL)==DOE_DX_BUSY);
    assert(unchanged==(doe_dx_shared_fence *)&source && point==999 && !initialized);
    puts("PASS SDK CPU shared waits: independent imported handle, pre-record GetDevice rollback, retained fence/device leases, ordered waits, partial wait failure suppresses Execute, explicit owner cleanup; no GPU");
}
