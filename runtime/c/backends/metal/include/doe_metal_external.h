/* SPDX-License-Identifier: MIT */
#ifndef DOE_METAL_EXTERNAL_H
#define DOE_METAL_EXTERNAL_H
#include "doe_metal_texture.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef enum doe_mtl_external_source {
    DOE_MTL_EXTERNAL_IOSURFACE = 1,
    DOE_MTL_EXTERNAL_CVPIXELBUFFER = 2
} doe_mtl_external_source;
typedef struct doe_mtl_external_planes {
    doe_mtl_texture *planes[2];
    uint32_t count;
} doe_mtl_external_planes;
/* Import caller-owned, completed IOSurface/CVPixelBuffer contents. The caller
 * synchronizes the producer before import and does not mutate/recycle storage
 * while a returned texture or command lease survives. Packed RGBA/BGRA and
 * two-plane 8-bit 420v/420f are admitted; no color conversion is implied.
 * On success each plane is an ordinary +1 texture, already initialized by its
 * external producer, admits sampling and copy-source blits, and uses the ordinary
 * command/pending lifetime. On failure
 * out is untouched and all privately imported native handles are released.
 * The native texture retains its IOSurface; source is borrowed only for import. */
doe_mtl_status doe_mtl_external_texture_import(doe_mtl_context *context,
    doe_mtl_external_source source, void *handle, doe_mtl_external_planes *out,
    doe_mtl_error *error);
#ifdef __cplusplus
}
#endif
#endif
