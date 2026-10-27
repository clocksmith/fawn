/* SPDX-License-Identifier: MIT */
#ifndef DOE_NATIVE_PERSISTENT_CACHE_H
#define DOE_NATIVE_PERSISTENT_CACHE_H
#include <stdbool.h>
/* Serialized by the existing device mutex. Reserve before unlocking for any
 * persistent access; an in-flight attempt must never race a successful disable. */
typedef enum native_persistent_cache_policy {
    NATIVE_PERSISTENT_CACHE_FRESH = 0,
    NATIVE_PERSISTENT_CACHE_USED,
    NATIVE_PERSISTENT_CACHE_DISABLED
} native_persistent_cache_policy;
static inline bool native_persistent_cache_reserve(native_persistent_cache_policy *policy)
{
    if (*policy == NATIVE_PERSISTENT_CACHE_DISABLED) return false;
    *policy = NATIVE_PERSISTENT_CACHE_USED;
    return true;
}
static inline bool native_persistent_cache_disable(native_persistent_cache_policy *policy)
{
    if (*policy == NATIVE_PERSISTENT_CACHE_USED) return false;
    *policy = NATIVE_PERSISTENT_CACHE_DISABLED;
    return true;
}
#endif
