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
 * Other formats hash current read-only mappings; ASLR/loader fixups can
 * conservatively prevent cross-process reuse. Unsupported
 * image enumeration fails closed, preserving digest; there is no disk fallback.
 * Runtime options/device/pipeline inputs must be independently bound by caller.
 */
bool native_shader_cache_identity(uint8_t digest[32]);
#endif
