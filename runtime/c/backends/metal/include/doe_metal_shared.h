/* SPDX-License-Identifier: MIT */
#ifndef DOE_METAL_SHARED_H
#define DOE_METAL_SHARED_H
#include "doe_metal_texture.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Borrowed handles below are real IOSurface / MTLSharedEvent identities. The
 * caller holds its context and serializes mutations. Import/create returns +1;
 * paired release is required before the context is released. Outputs remain
 * untouched on failure. Shared surfaces support one packed RGBA/BGRA plane. */
typedef struct doe_mtl_shared_surface_info {
    uint32_t width, height;
    doe_mtl_format format; /* Actual packed pixel format, never chosen by a later texture. */
} doe_mtl_shared_surface_info;
/* Packed physical format and dimensions are available at import, before any
 * texture exists. On failure retained/info stay untouched. Success transfers
 * one native surface lease, released through doe_mtl_shared_surface_release. */
/* Retains the same actual surface and publishes its dimensions/format together.
 * Every output stays untouched on failure; unknown or multiplane formats reject. */
doe_mtl_status doe_mtl_shared_surface_open_info(doe_mtl_context *context, void *surface,
    void **retained, doe_mtl_shared_surface_info *info, doe_mtl_error *error);
doe_mtl_status doe_mtl_shared_surface_open(doe_mtl_context *context, void *surface,
    void **retained, uint32_t *width, uint32_t *height, doe_mtl_error *error);
void doe_mtl_shared_surface_release(doe_mtl_context *context, void *surface);
doe_mtl_status doe_mtl_shared_texture_import(doe_mtl_context *context, void *surface,
    const doe_mtl_texture_desc *desc, doe_mtl_texture **out, doe_mtl_error *error);
doe_mtl_status doe_mtl_shared_event_import(doe_mtl_context *context, void *event,
    void **out, doe_mtl_error *error);
doe_mtl_status doe_mtl_shared_event_create(doe_mtl_context *context, void **out,
    doe_mtl_error *error);
void doe_mtl_shared_event_release(doe_mtl_context *context, void *event);
/* One admitted batch retains every event on the command until terminal
 * completion/abandonment. Encoding waits never reads or host-polls event values. */
typedef struct doe_mtl_shared_wait { void *event; uint64_t value; } doe_mtl_shared_wait;
doe_mtl_status doe_mtl_commands_wait_shared_events(doe_mtl_commands *commands,
    const doe_mtl_shared_wait *waits, size_t count, doe_mtl_error *error);
/* One completion signal per command. Takes an ordinary native event lease
 * before encoding and keeps it through terminal completion or abandonment;
 * unknown/pending commands cannot release that obligation. */
doe_mtl_status doe_mtl_commands_signal_shared_event(doe_mtl_commands *commands,
    void *event, uint64_t value, doe_mtl_error *error);
/* Latest committed queue point, borrowed under caller serialization. Used to
 * forward a producer dependency even when an access records no texture work. */
doe_mtl_status doe_mtl_context_release_point(const doe_mtl_context *, void **, uint64_t *);
/* Release point for a shared texture access without new GPU work: the context
 * timeline event (borrowed) and the value the queue signals at the end of the
 * committed command buffer that currently owns the texture, after all of its
 * work. With no owner every recorded use has retired, so the value is 0 (already
 * reached). UNSUPPORTED without a real timeline; INVALID_STATE while the owner
 * is still recording. Outputs stay untouched on failure. */
doe_mtl_status doe_mtl_texture_release_point(const doe_mtl_texture *texture,
    void **event, uint64_t *value);
#ifdef __cplusplus
}
#endif
#endif
