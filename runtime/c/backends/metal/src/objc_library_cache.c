/* SPDX-License-Identifier: MIT */
#include "objc_abi.h"

enum { LIBRARY_CACHE_SLOTS = 16, LIBRARY_SOURCE_LIMIT = 1024 * 1024 };
typedef struct library_entry {
    void *device, *library;
    char *source;
    size_t length;
} library_entry;
struct doe_mtl_library_cache {
    library_entry entries[LIBRARY_CACHE_SLOTS];
    size_t next;
};

void doe_mtl_objc_library_cache_open(doe_mtl_objc *runtime)
{
    if (runtime && !runtime->libraries) runtime->libraries = calloc(1,sizeof(*runtime->libraries));
}

static void drop_entry(doe_mtl_objc *runtime, library_entry *entry)
{
    if (entry->library) doe_mtl_objc_void(runtime,entry->library,runtime->release);
    if (entry->device) doe_mtl_objc_void(runtime,entry->device,runtime->release);
    free(entry->source);
    *entry = (library_entry){0};
}

void doe_mtl_objc_library_cache_close(doe_mtl_objc *runtime)
{
    if (!runtime || !runtime->libraries) return;
    for (size_t i = 0; i < LIBRARY_CACHE_SLOTS; ++i) drop_entry(runtime,&runtime->libraries->entries[i]);
    free(runtime->libraries);
    runtime->libraries = NULL;
}

void *doe_mtl_objc_library_lookup(doe_mtl_objc *runtime, void *device, const char *source, size_t length)
{
    if (!runtime || !runtime->libraries || !source || !length || length > LIBRARY_SOURCE_LIMIT) return NULL;
    for (size_t i = 0; i < LIBRARY_CACHE_SLOTS; ++i) {
        library_entry *entry = &runtime->libraries->entries[i];
        if (entry->library && entry->device == device && entry->length == length &&
            !memcmp(entry->source,source,length))
            return doe_mtl_objc_object(runtime,entry->library,runtime->retain);
    }
    return NULL;
}

void doe_mtl_objc_library_store(doe_mtl_objc *runtime, void *device,
    const char *source, size_t length, void *library)
{
    if (!runtime || !runtime->libraries || !device || !source || !length ||
        length > LIBRARY_SOURCE_LIMIT || !library) return;
    /* Cache allocation is optional: never fail a successful native compile. */
    char *copy = malloc(length);
    if (!copy) return;
    memcpy(copy,source,length);
    void *retained = doe_mtl_objc_object(runtime,library,runtime->retain);
    if (!retained) { free(copy); return; }
    void *retained_device = doe_mtl_objc_object(runtime,device,runtime->retain);
    if (!retained_device) {
        doe_mtl_objc_void(runtime,retained,runtime->release);
        free(copy);
        return;
    }
    struct doe_mtl_library_cache *cache = runtime->libraries;
    drop_entry(runtime,&cache->entries[cache->next]);
    cache->entries[cache->next] = (library_entry){retained_device,retained,copy,length};
    cache->next = (cache->next + 1) % LIBRARY_CACHE_SLOTS;
}

char *doe_mtl_objc_library_source(doe_mtl_objc *runtime, const char *source, size_t length)
{
    if (!runtime || !runtime->libraries || !source || !length || length > LIBRARY_SOURCE_LIMIT) return NULL;
    char *copy = malloc(length);
    if (copy) memcpy(copy,source,length);
    return copy;
}
