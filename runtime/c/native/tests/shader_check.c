/* SPDX-License-Identifier: MIT
 * Device-less WGSL check extension: accepted modules report success with an
 * empty diagnostic; rejected modules report a positioned compiler diagnostic. */
#include "doe_native_shader_check.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void check(const char *source, uint32_t expected)
{
    assert(doeNativeCheckShaderSource(source, strlen(source)) == expected);
}

int main(void)
{
    char text[256];
    check("this is not wgsl", 0);
    assert(doeNativeCopyLastErrorMessage(text, sizeof(text)) > 0 && text[0]);
    assert(doeNativeCopyLastErrorStage(text, sizeof(text)) > 0 && strcmp(text, "parser") == 0);
    assert(doeNativeCopyLastErrorKind(text, sizeof(text)) > 0 && strcmp(text, "ParseError") == 0);
    assert(doeNativeGetLastErrorLine() == 1 && doeNativeGetLastErrorColumn() == 1);
    char small[4];
    const size_t full = doeNativeCopyLastErrorMessage(NULL, 0);
    assert(doeNativeCopyLastErrorMessage(small, sizeof(small)) == full && strlen(small) == 3);

    check("@compute @workgroup_size(1) fn main() {\n  let x: u32 = undeclared;\n}\n", 0);
    assert(doeNativeCopyLastErrorStage(text, sizeof(text)) && strcmp(text, "sema") == 0);
    assert(doeNativeGetLastErrorLine() == 2);

    check("@group(0) @binding(0) var<storage, read_write> d: array<u32>;\n"
        "@compute @workgroup_size(64) fn main(@builtin(global_invocation_id) g: vec3u) { d[g.x] = g.x; }\n", 1);
    assert(doeNativeCopyLastErrorMessage(text, sizeof(text)) == 0 && text[0] == '\0');
    assert(doeNativeCopyLastErrorStage(text, sizeof(text)) == 0);
    assert(doeNativeGetLastErrorLine() == 0 && doeNativeGetLastErrorColumn() == 0);

    assert(doeNativeCheckShaderSource(NULL, 4) == 0);
    assert(doeNativeCopyLastErrorKind(text, sizeof(text)) && strcmp(text, "InvalidInput") == 0);
    puts("PASS device-less WGSL check accepts valid modules and reports positioned diagnostics");
    return 0;
}
