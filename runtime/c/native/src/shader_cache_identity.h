/* SPDX-License-Identifier: MIT */
#ifndef DOE_NATIVE_SHADER_CACHE_IDENTITY_H
#define DOE_NATIVE_SHADER_CACHE_IDENTITY_H
#include <stdbool.h>
#include <stdint.h>

/* Hash the live native/compiler/shader-owner images' executable and immutable data
 * mappings, not a pathname which may have been replaced after loading. Images
 * stay resident under the native owner contract. Call outside device locks.
 * ELF requires the mapped linker build ID and hashes originally read-only
 * payload; the build ID binds relocation-bearing data without process pointers.
 * Windows x64 follows the canonical compiler DLL IAT import (not a thunk);
 * the shader owner is statically composed by the native Windows source list.
 * Mach-O requires a nonzero LC_UUID and hashes originally read-only mapped
 * payload. The UUID binds relocation-bearing __DATA_CONST bytes without ASLR
 * pointers, even when dyld has made those pages read-only. Windows hashes
 * current read-only mappings; loader fixups can conservatively prevent
 * cross-process reuse. Unsupported
 * image enumeration fails closed, preserving digest; there is no disk fallback.
 * Runtime options/device/pipeline inputs must be independently bound by caller.
 */
bool native_shader_cache_identity(uint8_t digest[32]);
/* Digest of the loaded native and compiler images' ELF linker build IDs only
 * (no payload hashing). Locates embedder pipeline blobs; false elsewhere. */
bool native_build_identity(uint8_t digest[32]);
#endif
