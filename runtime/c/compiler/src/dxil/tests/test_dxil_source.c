/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "test_util.h"
#include "dxil_source.h"
#include <errno.h>
#include <fenv.h>
#include <stdio.h>
#include <stdlib.h>

static bool contains(const DxilSourceHlsl *result, const char *needle) {
    size_t n = strlen(needle);
    if (n > result->text_length) return false;
    for (size_t i = 0u; i <= result->text_length - n; ++i)
        if (memcmp(result->text + i, needle, n) == 0) return true;
    return false;
}

static void accepted(const char *label, const char *source, DxilStage stage,
    const char *marker) {
    DxilSourceHlsl result;
    DxilDiagnostic diagnostic;
    DxilDiagKind error = DXIL_DIAG_INVALID_IR;
    const bool ok = dxil_source_hlsl(source, strlen(source), &result, &diagnostic, &error);
    if (!ok) fprintf(stderr, "%s: %s\n", label, diagnostic.message);
    CHECK(ok);
    if (!ok) return;
    CHECK(result.text != NULL && result.text_length != 0u);
    CHECK(result.entry.stage == stage);
    CHECK(result.entry.name == result.entry_name);
    CHECK(strcmp(result.entry.name, "main") == 0);
    CHECK(result.entry.shader_model_major == 6u);
    if (marker != NULL) CHECK(contains(&result, marker));
    if (stage == DXIL_STAGE_COMPUTE) CHECK(result.entry.workgroup_size[0] == 1u);
    dxil_source_hlsl_free(&result);
    CHECK(result.text == NULL && result.text_length == 0u && result.entry.name == NULL);
    dxil_source_hlsl_free(&result);
}

static void source_forms(void) {
    accepted("for", "@group(0) @binding(0) var<storage,read_write> output:array<u32>;"
        "@compute @workgroup_size(1) fn main(){for(var i=0u;i<4u;i+=1u){output[i]=i;}}",
        DXIL_STAGE_COMPUTE, "RWStructuredBuffer");
    accepted("forward struct", "@compute @workgroup_size(1) fn main(){let x=Later(3u); _=x.value;}"
        "struct Later{value:u32,}", DXIL_STAGE_COMPUTE, NULL);
    accepted("global expression", "const offset=vec2i(-2,3)+vec2i(1,2);"
        "@group(0) @binding(0) var<storage,read_write> output:array<i32>;"
        "@compute @workgroup_size(1) fn main(){output[0]=offset.x;}", DXIL_STAGE_COMPUTE, NULL);
    accepted("matrix index", "@group(0) @binding(0) var<storage,read_write> output:array<f32>;"
        "@compute @workgroup_size(1) fn main(){var m=mat2x2f(1,2,3,4);output[0]=m[1].x;}",
        DXIL_STAGE_COMPUTE, NULL);
    accepted("override id/default", "@id(7) override amount:u32=3u; override unused:u32;"
        "@group(0) @binding(0) var<storage,read_write> output:array<u32>;"
        "@compute @workgroup_size(1) fn main(){output[0]=amount;}", DXIL_STAGE_COMPUTE, NULL);
    accepted("vertex", "@vertex fn main(@builtin(vertex_index) index:u32)->@builtin(position) vec4f"
        "{return vec4f(f32(index),0,0,1);}", DXIL_STAGE_VERTEX, "SV_Position");
    accepted("fragment", "@fragment fn main()->@location(0) vec4f{return vec4f(0,1,0,1);}",
        DXIL_STAGE_FRAGMENT, "SV_Target");
    accepted("atomic", "@group(0) @binding(0) var<storage,read_write> output:array<atomic<u32>>;"
        "@compute @workgroup_size(1) fn main(){_=atomicAdd(&output[0],1u);}",
        DXIL_STAGE_COMPUTE, "InterlockedAdd");
    accepted("packed", "requires packed_4x8_integer_dot_product;"
        "@group(0) @binding(0) var<storage,read_write> output:array<u32>;"
        "@compute @workgroup_size(1) fn main(){output[0]=pack4xU8(vec4u(1,2,3,4));}",
        DXIL_STAGE_COMPUTE, NULL);
    accepted("pointer local", "@group(0) @binding(0) var<storage,read_write> output:array<u32>;"
        "@compute @workgroup_size(1) fn main(){let destination=&output[0];*destination=7u;}",
        DXIL_STAGE_COMPUTE, NULL);
    accepted("runtime array write clamp", "@group(0) @binding(0) var<storage,read_write> output:array<u32>;"
        "@compute @workgroup_size(1) fn main(@builtin(global_invocation_id) gid:vec3u){output[gid.x]=7u;}",
        DXIL_STAGE_COMPUTE, "(max(doe_r0_bytes() / 4u, 1u) - 1u)");
    accepted("first entry", "@compute @workgroup_size(1) fn main(){}"
        "@vertex fn later()->@builtin(position) vec4f{return vec4f(0,0,0,1);}",
        DXIL_STAGE_COMPUTE, NULL);
}

static void rejected(void) {
    static const char *const sources[] = {
        "@compute @workgroup_size(1) fn main(){let x:u32=true;}",
        "@group(0) @binding(0) var<storage,read_write> a:array<u32>;"
        "@group(0) @binding(0) var<storage,read_write> b:array<u32>;"
        "@compute @workgroup_size(1) fn main(){a[0]=b[0];}",
        "@compute @workgroup_size(1) fn main(){} fn unused(){missing();}",
        "@id(7) override a:u32=1u; @id(7) override b:u32=2u;"
        "@compute @workgroup_size(1) fn main(){}",
        "override a:u32; @group(0) @binding(0) var<storage,read_write> output:array<u32>;"
        "@compute @workgroup_size(1) fn main(){output[0]=a;}",
        "fn no_entry(){}"
    };
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
        DxilSourceHlsl result;
        DxilDiagnostic diagnostic;
        DxilDiagKind error = DXIL_DIAG_INVALID_IR;
        CHECK(!dxil_source_hlsl(sources[i], strlen(sources[i]), &result, &diagnostic, &error));
        CHECK(result.text == NULL && result.entry.name == NULL);
        CHECK(diagnostic.has_kind && diagnostic.message_len != 0u);
        dxil_source_hlsl_free(&result);
    }
}

static void grown_pools(void) {
    /* Cross the old native adapter's function/local/statement bounds and the
     * host adapter's initial token/expression/statement pools. This must reuse
     * the canonical typed pool reports, not retry arbitrary semantic failures. */
    char source[65536];
    size_t used = 0u;
    for (unsigned i = 0u; i < 80u; ++i) {
        int n = snprintf(source + used, sizeof(source) - used,
            "fn helper%u()->u32{return %uu;}\n", i, i);
        CHECK(n > 0 && (size_t)n < sizeof(source) - used);
        if (n <= 0 || (size_t)n >= sizeof(source) - used) return;
        used += (size_t)n;
    }
    int n = snprintf(source + used, sizeof(source) - used,
        "@compute @workgroup_size(1) fn main(){\n");
    CHECK(n > 0 && (size_t)n < sizeof(source) - used);
    if (n <= 0 || (size_t)n >= sizeof(source) - used) return;
    used += (size_t)n;
    for (unsigned i = 0u; i < 600u; ++i) {
        n = snprintf(source + used, sizeof(source) - used,
            "let local%u=helper%u()+%uu;\n", i, i % 80u, i);
        CHECK(n > 0 && (size_t)n < sizeof(source) - used);
        if (n <= 0 || (size_t)n >= sizeof(source) - used) return;
        used += (size_t)n;
    }
    source[used++] = '}';
    source[used] = '\0';
    accepted("grown pools", source, DXIL_STAGE_COMPUTE, NULL);
}

static void failure_ownership(void) {
    static const char embedded_nul[] = "@compute\0 @workgroup_size(1) fn main(){}";
    DxilSourceHlsl result;
    DxilDiagnostic diagnostic;
    DxilDiagKind error = DXIL_DIAG_INVALID_IR;
    CHECK(!dxil_source_hlsl(embedded_nul, sizeof(embedded_nul) - 1u,
        &result, &diagnostic, &error));
    CHECK(result.text == NULL && result.entry.name == NULL);
    CHECK(diagnostic.has_kind && diagnostic.context_len != 0u);
    CHECK(!dxil_source_hlsl(NULL, 1u, &result, &diagnostic, &error));
    CHECK(result.text == NULL && result.entry.name == NULL);
    CHECK(!dxil_source_hlsl("", 0u, NULL, &diagnostic, &error));
    CHECK(!dxil_source_hlsl("", 0u, &result, &diagnostic, &error));
    CHECK(result.text == NULL && result.entry.name == NULL);
    CHECK(!dxil_source_hlsl("", 2u * 1024u * 1024u + 1u,
        &result, NULL, NULL));
    CHECK(result.text == NULL && result.entry.name == NULL);
    dxil_source_hlsl_free(NULL);
}

static void native_half_metadata(void) {
    static const char source[] = "enable f16;"
        "@group(0) @binding(0) var<storage,read_write> output:array<f16>;"
        "@compute @workgroup_size(1) fn main(){output[0]=1.0h;}";
    DxilSourceHlsl result;
    DxilDiagnostic diagnostic;
    CHECK(dxil_source_hlsl(source, sizeof(source) - 1u, &result, &diagnostic, NULL));
    CHECK(result.entry.native16);
    CHECK(result.entry.shader_model_major == 6u && result.entry.shader_model_minor == 2u);
    CHECK(contains(&result, "float16_t"));
    dxil_source_hlsl_free(&result);
}

int main(void) {
    errno = ERANGE;
    CHECK(fesetround(FE_DOWNWARD) == 0);
    CHECK(feraiseexcept(FE_INVALID) == 0);
    source_forms();
    rejected();
    grown_pools();
    failure_ownership();
    native_half_metadata();
    CHECK(errno == ERANGE);
    CHECK(fegetround() == FE_DOWNWARD);
    CHECK((fetestexcept(FE_INVALID) & FE_INVALID) != 0);
    CHECK(fesetround(FE_TONEAREST) == 0);
    CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
    TEST_SUMMARY("shared DXIL source");
}
