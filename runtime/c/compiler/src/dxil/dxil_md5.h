/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_DXIL_MD5_H
#define DOE_DXIL_MD5_H

#include <stddef.h>
#include <stdint.h>

/* MD5 message digest (RFC 1321), as used by the DXBC container hash.
 * The Zig container code calls std.crypto.hash.Md5 over the container
 * bytes starting at offset 20; this is the same algorithm. The caller
 * owns both buffers; `out` must hold 16 bytes. */
void dxil_md5(const uint8_t *data, size_t len, uint8_t out[16]);

#endif /* DOE_DXIL_MD5_H */
