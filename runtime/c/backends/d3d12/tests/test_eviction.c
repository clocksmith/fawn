/* SPDX-License-Identifier: MIT */
#include "mock_native.h"
#include <stdio.h>

static doe_dx_context *open_context(void) {
    doe_dx_context *context = NULL;
    assert(doe_dx_context_create(NULL, &context, NULL) == DOE_DX_OK);
    return context;
}

static void idle_and_lent(void) {
    doe_dx_context *context = open_context();
    doe_dx_buffer *buffer = NULL;
    assert(doe_dx_buffer_create_readback_zeroed(context, 64, &buffer, NULL) == DOE_DX_OK);
    const void *bytes = NULL;
    assert(doe_dx_buffer_lend_read(buffer, 0, 64, &bytes, NULL) == DOE_DX_OK);
    assert(bytes && buffer->lent && mock.buffers == 1);
    assert(doe_dx_buffer_evict(buffer) == DOE_DX_OK && !buffer->native && !buffer->lent);
    assert(!mock.buffers && doe_dx_buffer_size(buffer) == 64);
    assert(doe_dx_buffer_evict(buffer) == DOE_DX_OK);
    unsigned char untouched = 0x6d;
    assert(doe_dx_buffer_read(buffer, 0, &untouched, 1, NULL) == DOE_DX_INVALID_STATE && untouched == 0x6d);
    doe_dx_buffer_release(buffer);
    doe_dx_texture *texture = NULL;
    doe_dx_texture_desc desc = {1, 1, DOE_DX_RGBA8_UNORM, DOE_DX_TEXTURE_COPY_SRC | DOE_DX_TEXTURE_COPY_DST};
    assert(doe_dx_texture_create(context, &desc, &texture, NULL) == DOE_DX_OK);
    assert(doe_dx_texture_evict(texture) == DOE_DX_OK && !texture->native && !mock.textures);
    doe_dx_texture_desc observed = {0};
    assert(doe_dx_texture_description(texture, &observed) == DOE_DX_OK && observed.width == 1);
    doe_dx_texture_view_desc view = {0};
    assert(doe_dx_texture_view_validate(texture, NULL, &view) == DOE_DX_INVALID_STATE);
    assert(doe_dx_texture_evict(texture) == DOE_DX_OK);
    doe_dx_texture_release(texture);
    assert(doe_dx_context_destroy(context) == DOE_DX_OK);
    mock_reset();
}

static void recorded_and_pending(unsigned mode) {
    doe_dx_context *context = open_context();
    doe_dx_buffer *source = NULL, *destination = NULL;
    doe_dx_texture *texture = NULL;
    doe_dx_commands *work = NULL, *later = NULL;
    assert(doe_dx_buffer_create(context, 64, DOE_DX_UPLOAD, &source, NULL) == DOE_DX_OK);
    assert(doe_dx_buffer_create(context, 64, DOE_DX_DEFAULT, &destination, NULL) == DOE_DX_OK);
    doe_dx_texture_desc desc = {1, 1, DOE_DX_RGBA8_UNORM, DOE_DX_TEXTURE_COPY_DST};
    assert(doe_dx_texture_create(context, &desc, &texture, NULL) == DOE_DX_OK);
    assert(doe_dx_commands_create(context, &work, NULL) == DOE_DX_OK);
    assert(doe_dx_commands_copy(work, source, 0, destination, 0, 64, NULL) == DOE_DX_OK);
    assert(doe_dx_commands_initialize_texture(work, texture, NULL) == DOE_DX_OK);
    if (mode > 1) {
        mock.failure = mode == 3 ? MOCK_SIGNAL : MOCK_NONE;
        assert(doe_dx_commands_submit(work, NULL) == (mode == 3 ? DOE_DX_COMPLETION_UNKNOWN : DOE_DX_OK));
    }
    size_t allocations = mock.buffers;
    assert(doe_dx_buffer_evict(source) == DOE_DX_OK);
    assert(doe_dx_buffer_evict(destination) == DOE_DX_OK);
    assert(doe_dx_texture_evict(texture) == DOE_DX_OK);
    assert(source->native && destination->native && texture->native);
    assert(mock.buffers == allocations && mock.textures == 1);
    assert(doe_dx_commands_create(context, &later, NULL) == DOE_DX_OK);
    assert(doe_dx_commands_copy(later, source, 0, destination, 0, 64, NULL) == DOE_DX_INVALID_STATE);
    assert(doe_dx_commands_initialize_texture(later, texture, NULL) == DOE_DX_INVALID_STATE);
    doe_dx_commands_release(later);
    if (mode == 1) assert(doe_dx_commands_submit(work, NULL) == DOE_DX_OK);
    doe_dx_commands_release(work);
    if (mode) {
        if (mode == 3) {
            mock.completed = 1;
            assert(doe_dx_context_poll(context, 0, NULL) == DOE_DX_COMPLETION_UNKNOWN);
            assert(source->native && destination->native && texture->native);
        }
        mock.failure = MOCK_NONE;
        mock.completed = 1;
        assert(doe_dx_context_poll(context, 0, NULL) == DOE_DX_OK);
    }
    assert(!source->native && !destination->native && !texture->native);
    assert(!mock.buffers && !mock.textures);
    doe_dx_buffer_release(source);
    doe_dx_buffer_release(destination);
    doe_dx_texture_release(texture);
    assert(doe_dx_context_destroy(context) == DOE_DX_OK);
    mock_reset();
}

static void borrowed_surface(void) {
    doe_dx_context *context = open_context();
    doe_dx_surface_desc desc = {.width=1, .height=1, .format=DOE_DX_RGBA8_UNORM,
        .usage=DOE_DX_TEXTURE_RENDER_ATTACHMENT};
    doe_dx_surface *surface = NULL;
    doe_dx_commands *work = NULL;
    doe_dx_texture *texture = NULL;
    assert(doe_dx_surface_create(context, context, &desc, &surface, NULL) == DOE_DX_OK);
    assert(doe_dx_commands_create(context, &work, NULL) == DOE_DX_OK);
    assert(doe_dx_surface_acquire(surface, work, &texture, NULL) == DOE_DX_OK);
    assert(doe_dx_texture_evict(texture) == DOE_DX_BUSY && !texture->evict_requested && texture->native);
    doe_dx_commands_release(work);
    doe_dx_texture_release(texture);
    doe_dx_surface_release(surface);
    assert(doe_dx_context_destroy(context) == DOE_DX_OK);
    mock_reset();
}

int main(void) {
    mock_reset();
    idle_and_lent();
    for (unsigned mode = 0; mode < 4; ++mode) recorded_and_pending(mode);
    borrowed_surface();
    puts("D3D12 eviction: idle/lent, recorded/discarded, accepted, unknown and surface controls passed; host only");
    return 0;
}
