/* SPDX-License-Identifier: MIT */
#include "mock_native.h"
#include <stdio.h>

int main(void)
{
    mock_reset();
    doe_dx_context *context = NULL;
    assert(doe_dx_context_create(NULL,&context,NULL) == DOE_DX_OK);
    /* Additive subresource layout admits exactly one buffer-copy plane. */
    doe_dx_texture_desc combined_desc={4,3,DOE_DX_D32_FLOAT_S8_UINT,
        DOE_DX_TEXTURE_DEPTH_ATTACHMENT|DOE_DX_TEXTURE_COPY_SRC|DOE_DX_TEXTURE_COPY_DST};
    doe_dx_texture *combined=NULL;
    assert(doe_dx_texture_create(context,&combined_desc,&combined,NULL)==DOE_DX_OK);
    doe_dx_texture_region_ex plane={.rect={0,0,4,3}};
    doe_dx_texture_footprint plane_layout={.span=123},kept_plane=plane_layout;
    assert(doe_dx_texture_region_layout(combined,plane,(doe_dx_texture_layout){0},&plane_layout)==DOE_DX_INVALID_ARGUMENT);
    assert(!memcmp(&plane_layout,&kept_plane,sizeof(plane_layout)));
    plane.aspect=1;
    assert(doe_dx_texture_region_layout(combined,plane,(doe_dx_texture_layout){0},&plane_layout)==DOE_DX_OK);
    assert(plane_layout.row_bytes==16 && plane_layout.rows==3);
    plane.aspect=2;
    assert(doe_dx_texture_region_layout(combined,plane,(doe_dx_texture_layout){0},&plane_layout)==DOE_DX_OK);
    assert(plane_layout.row_bytes==4 && plane_layout.rows==3);
    doe_dx_texture_release(combined);
    doe_dx_texture_desc desc = {4,3,DOE_DX_RGBA8_UINT,DOE_DX_TEXTURE_COPY_SRC|DOE_DX_TEXTURE_COPY_DST};
    doe_dx_texture *source = NULL, *destination = NULL;
    assert(doe_dx_texture_create(context,&desc,&source,NULL) == DOE_DX_OK);
    assert(doe_dx_texture_create(context,&desc,&destination,NULL) == DOE_DX_OK);
    doe_dx_texture_region region = {1,1,2,2};
    doe_dx_texture_footprint layout = {0};
    assert(doe_dx_texture_region_footprint(&desc,region,(doe_dx_texture_layout){0},&layout) == DOE_DX_OK);
    assert(layout.row_bytes == 8 && layout.rows == 2 && layout.span == 264);
    doe_dx_texture_footprint saved = layout;
    assert(doe_dx_texture_region_footprint(&desc,(doe_dx_texture_region){3,0,2,1},
        (doe_dx_texture_layout){0},&layout) == DOE_DX_RANGE);
    assert(!memcmp(&layout,&saved,sizeof(layout)));
    desc.format = DOE_DX_D32_FLOAT;
    assert(doe_dx_texture_region_footprint(&desc,region,(doe_dx_texture_layout){0},&layout) == DOE_DX_UNSUPPORTED);
    assert(!memcmp(&layout,&saved,sizeof(layout)));
    doe_dx_buffer *zero = NULL, *native = NULL;
    assert(doe_dx_buffer_create(context,528,DOE_DX_UPLOAD,&zero,NULL) == DOE_DX_OK);
    assert(doe_dx_buffer_create(context,528,DOE_DX_DEFAULT,&native,NULL) == DOE_DX_OK);
    doe_dx_commands *commands = NULL;
    assert(doe_dx_commands_create(context,&commands,NULL) == DOE_DX_OK);
    mock.allocation_failure = mock.allocation_attempts + 1;
    assert(doe_dx_commands_texture_region(commands,zero,(doe_dx_texture_layout){0},source,
        (doe_dx_texture_region){0,0,4,3},true,NULL) == DOE_DX_OUT_OF_MEMORY);
    assert(!commands->texture_count && !commands->buffer_count && !source->owner && !zero->owner);
    mock.allocation_failure = 0;
    assert(doe_dx_commands_texture_region(commands,native,(doe_dx_texture_layout){0},source,region,true,NULL) == DOE_DX_UNINITIALIZED);
    assert(!commands->texture_count && !commands->buffer_count && !mock.texture_transfers);
    assert(doe_dx_commands_copy(commands,zero,0,native,0,528,NULL) == DOE_DX_OK);
    assert(doe_dx_commands_texture_region(commands,native,(doe_dx_texture_layout){0},source,region,true,NULL) == DOE_DX_UNINITIALIZED);
    assert(!commands->texture_count && !mock.texture_transfers);
    assert(doe_dx_commands_upload_texture(commands,zero,(doe_dx_texture_layout){0},source,NULL) == DOE_DX_OK);
    assert(doe_dx_commands_upload_texture(commands,zero,(doe_dx_texture_layout){0},destination,NULL) == DOE_DX_OK);
    assert(doe_dx_commands_texture_region(commands,native,(doe_dx_texture_layout){0},source,region,true,NULL) == DOE_DX_OK);
    assert(mock.upload && mock.region.x == 1 && mock.region.y == 1 && mock.footprint.span == 264);
    assert(doe_dx_commands_texture_region(commands,native,(doe_dx_texture_layout){0},source,region,false,NULL) == DOE_DX_OK);
    assert(!mock.upload && commands->buffer_count == 2 && commands->texture_count == 2);
    assert(doe_dx_commands_blit_texture(commands,source,region,destination,0,0,NULL) == DOE_DX_OK);
    assert(mock.texture_blits == 1 && mock.destination_x == 0 && mock.region.width == 2);
    assert(doe_dx_commands_blit_texture(commands,source,region,destination,3,0,NULL) == DOE_DX_RANGE);
    assert(doe_dx_commands_blit_texture(commands,source,region,source,0,0,NULL) == DOE_DX_UNSUPPORTED);
    assert(mock.texture_blits == 1);
    doe_dx_commands *other = NULL;
    assert(doe_dx_commands_create(context,&other,NULL) == DOE_DX_OK);
    assert(doe_dx_commands_texture_region(other,zero,(doe_dx_texture_layout){0},source,region,true,NULL) == DOE_DX_BUSY);
    doe_dx_commands_release(other);
    doe_dx_texture_release(source);
    doe_dx_texture_release(destination);
    doe_dx_buffer_release(zero);
    doe_dx_buffer_release(native);
    mock.failure = MOCK_SIGNAL;
    assert(doe_dx_commands_submit(commands,NULL) == DOE_DX_COMPLETION_UNKNOWN);
    doe_dx_commands_release(commands);
    assert(mock.textures == 2 && mock.buffers == 2);
    mock.failure = MOCK_NONE;
    assert(doe_dx_context_poll(context,0,NULL) == DOE_DX_PENDING);
    mock.completed = 1;
    assert(doe_dx_context_poll(context,0,NULL) == DOE_DX_OK);
    assert(!mock.textures && !mock.buffers && !mock.commands);
    assert(doe_dx_context_destroy(context) == DOE_DX_OK);
    mock_reset();
    puts("PASS private D3D12 texture regions: typed bounds, DEFAULT transfer, init, padding obligation, aliases, unknown leases; no SDK/GPU");
}
