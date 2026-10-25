/* SPDX-License-Identifier: MIT */
#include "sha256.h"
#include "../../common/include/doe_sha256.h"
void doe_sha256(const void *data, size_t size, uint8_t digest[32])
{
    doe_common_sha256(data, size, digest);
}
void doe_sha256_hex(const uint8_t digest[32], char out[65])
{
    doe_common_sha256_hex(digest, out);
}
