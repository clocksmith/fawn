/* SPDX-License-Identifier: MIT */
#ifndef DOE_MTL_OBJC_EXTERNAL_TYPES_H
#define DOE_MTL_OBJC_EXTERNAL_TYPES_H
/* Exact IOSurface/CoreVideo C and newTextureWithDescriptor:iosurface:plane:
 * method signatures. Sizes and planes use size_t / NSUInteger, formats OSType. */
typedef void *(*doe_objc_external_texture_call)(void *, void *, void *, void *, size_t);
typedef struct doe_mtl_objc_external {
    void *iosurface_library, *corevideo_library;
    void *new_texture;
    doe_objc_external_texture_call texture_call;
    size_t (*surface_planes)(void *);
    size_t (*surface_width)(void *);
    size_t (*surface_height)(void *);
    size_t (*surface_plane_width)(void *, size_t);
    size_t (*surface_plane_height)(void *, size_t);
    uint32_t (*surface_format)(void *);
    void *(*pixel_surface)(void *);
    size_t (*pixel_planes)(void *);
    size_t (*pixel_width)(void *);
    size_t (*pixel_height)(void *);
    size_t (*pixel_plane_width)(void *, size_t);
    size_t (*pixel_plane_height)(void *, size_t);
    uint32_t (*pixel_format)(void *);
} doe_mtl_objc_external;
#endif
