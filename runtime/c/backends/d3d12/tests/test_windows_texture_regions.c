/* SPDX-License-Identifier: MIT
 * Actual SDK CPU call-composition control. No adapter, device or GPU is created. */
#include "../src/windows_internal.h"
#include <assert.h>
#include <stdio.h>

typedef struct observed {
    D3D12_TEXTURE_COPY_LOCATION source, destination;
    D3D12_BOX box;
    bool has_box;
    UINT x, y, z;
    size_t copies, transitions;
    ID3D12Resource *resource[2];
    D3D12_RESOURCE_STATES before[2], after[2];
} observed;
static observed seen;
static void STDMETHODCALLTYPE copy_region(ID3D12GraphicsCommandList *self,
    const D3D12_TEXTURE_COPY_LOCATION *destination,UINT x,UINT y,UINT z,
    const D3D12_TEXTURE_COPY_LOCATION *source,const D3D12_BOX *box)
{
    (void)self;
    seen.source = *source;
    seen.destination = *destination;
    seen.x = x; seen.y = y; seen.z = z;
    seen.has_box = box != NULL;
    if (box) seen.box = *box;
    seen.copies++;
}
void doe_dx_transition(ID3D12GraphicsCommandList *list,ID3D12Resource *resource,
    D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after)
{
    (void)list;
    assert(seen.transitions < 2);
    size_t i = seen.transitions++;
    seen.resource[i] = resource;
    seen.before[i] = before;
    seen.after[i] = after;
}
static void restored(ID3D12Resource *resource)
{
    assert(seen.transitions == 2 && seen.resource[0] == resource && seen.resource[1] == resource);
    assert(seen.before[0] == D3D12_RESOURCE_STATE_COPY_DEST && seen.after[0] == D3D12_RESOURCE_STATE_COPY_SOURCE);
    assert(seen.before[1] == D3D12_RESOURCE_STATE_COPY_SOURCE && seen.after[1] == D3D12_RESOURCE_STATE_COPY_DEST);
}
int main(void)
{
    ID3D12GraphicsCommandListVtbl methods = {0};
    methods.CopyTextureRegion = copy_region;
    ID3D12GraphicsCommandList list = {&methods};
    ID3D12Resource buffer_resource = {0}, texture_resource = {0}, destination_resource = {0};
    doe_dx_native_commands commands = {0};
    commands.list = &list;
    doe_dx_native_buffer buffer = {.resource=&buffer_resource,.size=1024,.memory=DOE_DX_DEFAULT};
    doe_dx_native_texture texture = {.resource=&texture_resource};
    texture.description.Format = DXGI_FORMAT_R8G8B8A8_UINT;
    doe_dx_texture_footprint footprint = {.offset=512,.span=264,.row_pitch=256,.row_bytes=8,.rows=2};
    doe_dx_texture_region region = {3,4,2,2};
    doe_dx_native_texture_region_transfer(&commands,&buffer,&texture,&footprint,region,true);
    assert(seen.copies == 1 && !seen.has_box && seen.x == 3 && seen.y == 4 && !seen.z);
    assert(seen.destination.pResource == &texture_resource && seen.destination.Type == D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX);
    assert(seen.source.pResource == &buffer_resource && seen.source.Type == D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT);
    assert(seen.source.PlacedFootprint.Offset == 512 && seen.source.PlacedFootprint.Footprint.Width == 2 &&
        seen.source.PlacedFootprint.Footprint.Height == 2 && seen.source.PlacedFootprint.Footprint.RowPitch == 256 &&
        seen.source.PlacedFootprint.Footprint.Format == DXGI_FORMAT_R8G8B8A8_UINT);
    restored(&buffer_resource);
    memset(&seen,0,sizeof(seen));
    buffer.memory = DOE_DX_UPLOAD;
    doe_dx_native_texture_region_transfer(&commands,&buffer,&texture,&footprint,region,true);
    assert(seen.copies == 1 && !seen.transitions);
    memset(&seen,0,sizeof(seen));
    buffer.memory = DOE_DX_DEFAULT;
    doe_dx_native_texture_region_transfer(&commands,&buffer,&texture,&footprint,region,false);
    assert(seen.copies == 1 && seen.has_box && !seen.x && !seen.y && !seen.z);
    assert(seen.source.pResource == &texture_resource && seen.destination.pResource == &buffer_resource);
    assert(seen.box.left == 3 && seen.box.top == 4 && seen.box.right == 5 && seen.box.bottom == 6 &&
        !seen.box.front && seen.box.back == 1);
    restored(&texture_resource);
    memset(&seen,0,sizeof(seen));
    doe_dx_native_texture destination = {.resource=&destination_resource};
    doe_dx_native_texture_blit(&commands,&texture,region,&destination,7,8);
    assert(seen.copies == 1 && seen.has_box && seen.x == 7 && seen.y == 8 && !seen.z);
    assert(seen.source.pResource == &texture_resource && seen.destination.pResource == &destination_resource);
    assert(seen.box.left == 3 && seen.box.top == 4 && seen.box.right == 5 && seen.box.bottom == 6);
    restored(&texture_resource);
    puts("PASS actual Windows SDK texture-region call composition; CPU-only, no device/GPU");
}
