/* SPDX-License-Identifier: MIT
 * SHA-256 equals the FIPS 180-4 / NIST CAVP vectors, including the padding
 * boundaries at 55, 56 and 64 bytes and a multi-block message (host only). */
#include "sha256.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void expect(const void *data, size_t size, const char *hex)
{
    uint8_t digest[32];
    char out[65];
    doe_sha256(data, size, digest);
    doe_sha256_hex(digest, out);
    if (strcmp(out, hex) != 0) {
        fprintf(stderr, "sha256 of %zu bytes: %s, expected %s\n", size, out, hex);
        abort();
    }
}

int main(void)
{
    expect("", 0, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    expect("abc", 3, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    expect("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56,
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    const size_t million = 1000000;
    char *a = malloc(million);
    assert(a != NULL);
    memset(a, 'a', million);
    expect(a, 55, "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318");
    expect(a, 64, "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb");
    expect(a, million, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    free(a);
    puts("runtime-cli sha256: FIPS 180-4 vectors including padding boundaries and 10^6 bytes (host only)");
    return 0;
}
