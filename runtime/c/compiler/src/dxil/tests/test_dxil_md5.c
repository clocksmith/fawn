/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include <string.h>

#include "dxil_md5.h"
#include "test_util.h"

static void check_hex(const uint8_t *digest, const char *expected_hex,
    const char *label) {
    static const char digits[] = "0123456789abcdef";
    char actual[33];
    int i = 0;
    for (i = 0; i < 16; i++) {
        actual[i * 2] = digits[digest[i] >> 4];
        actual[i * 2 + 1] = digits[digest[i] & 0xFu];
    }
    actual[32] = '\0';
    CHECK_MSG(strcmp(actual, expected_hex) == 0, "%s: got %s, expected %s",
        label, actual, expected_hex);
}

int main(void) {
    uint8_t digest[16];
    char buf_a[65];
    memset(buf_a, 'a', sizeof(buf_a));

    dxil_md5(NULL, 0, digest);
    check_hex(digest, "d41d8cd98f00b204e9800998ecf8427e", "md5 empty");
    dxil_md5((const uint8_t *)"abc", 3, digest);
    check_hex(digest, "900150983cd24fb0d6963f7d28e17f72", "md5 abc");
    dxil_md5((const uint8_t *)"The quick brown fox jumps over the lazy dog",
        43, digest);
    check_hex(digest, "9e107d9d372bb6826bd81d3542a419d6", "md5 fox");
    dxil_md5((const uint8_t *)
        "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56,
        digest);
    check_hex(digest, "8215ef0796a20bcaaae116d3876c664a", "md5 56-char");
    dxil_md5((const uint8_t *)buf_a, 55, digest);
    check_hex(digest, "ef1772b6dff9a122358552954ad0df65", "md5 55 a");
    dxil_md5((const uint8_t *)buf_a, 56, digest);
    check_hex(digest, "3b0c8ac703f828b04c6c197006d17218", "md5 56 a");
    dxil_md5((const uint8_t *)buf_a, 64, digest);
    check_hex(digest, "014842d480b571495a4a0363793f7367", "md5 64 a");

    TEST_SUMMARY("test_dxil_md5");
}
