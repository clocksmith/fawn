/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "../browser/job.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check(const char *source, bool enabled, const char *expected) {
    size_t size = 0u;
    char *result = compiler_browser_job(source, strlen(source), enabled, &size);
    assert(result != NULL && strlen(result) == size);
    if (strstr(result, expected) == NULL) {
        fprintf(stderr, "expected %s\nreceived %s\n", expected, result);
        abort();
    }
    free(result);
}

int main(void) {
    const char *source = "fn f(x:u32)->u32{return x/8u+x%4u;}";
    check(source, true, "return (x>> 3u)+(x& 3u);");
    check(source, false, source);
    check("const x:u32=32u;fn f()->u32{return x/8u;}", true, "return (x>> 3u);");
    check("fn f(x:i32)->i32{return x/8i;}", true, "\"rewrites\":0");
    check("fn f(x:vec2u)->vec2u{return x/8u;}", true, "\"rewrites\":0");
    check("fn f(x:u32)->u32{return (x)/8u;}", true, "\"rewrites\":0");
    check("fn f(x:u32)->u32{return x/(8u);}", true, "\"rewrites\":0");
    check("fn f(x:u32)->u32{return x/ /*keep*/ 8u;}", true, "\"rewrites\":0");
    check("fn f(x:u32)->u32{return x/8;}", true, "\"rewrites\":0");
    check("fn f(x:u32)->u32{return x/3u;}", true, "\"rewrites\":0");
    check("struct S{x:u32} fn f(s:S)->u32{return s.x/0x80000000u;}", true, "(s.x>> 31u)");
    check("fn alias(){}", false, "\"ok\":false");
    check("fn f(x:u32)->u32{return x/0u;}", false, "\"ok\":false");
    check("const f=-3.4028234663852886e+38f;", true, "\"ok\":true");
    for (unsigned i = 0u; i < 300u; ++i) check(source, true, "\"rewrites\":2");
    char *large = malloc(65536u + 1u);
    assert(large != NULL);
    memset(large, ' ', 65536u);
    large[65536u] = '\0';
    check(large, true, "\"ok\":true");
    free(large);
    puts("C browser source rewrite: typed edits, preserved source, validation and job lifetime PASS");
    return 0;
}
