/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "../tsir_source.h"
#include "../tsir_digest.h"
#include <errno.h>
#include <fenv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
static void check(bool value, const char *message) {
    ++checks;
    if (!value) { fprintf(stderr, "TSIR source failure: %s\n", message); exit(1); }
}
static char *read_fixture(const char *name, size_t *length) {
    char path[256];
    int count = snprintf(path, sizeof(path), "runtime/zig/tests/tsir/bootstrap/%s.wgsl", name);
    check(count > 0 && (size_t)count < sizeof(path), "fixture path");
    FILE *file = fopen(path, "rb");
    check(file != NULL, "open fixture");
    char *bytes = malloc((1u << 20) + 1u);
    check(bytes != NULL, "fixture storage");
    *length = fread(bytes, 1u, 1u << 20, file);
    check(!ferror(file), "fixture read");
    check(fclose(file) == 0, "fixture close");
    bytes[*length] = '\0';
    return bytes;
}
static TsirSemantic lower(const char *text, size_t length) {
    TsirSemantic semantic = {0};
    DoeCompilerDiagnostic diagnostic;
    errno = EDOM;
    check(tsir_source_lower(text, length, "frontend-bootstrap-pipeline-v1", &semantic, &diagnostic) == TSIR_OK,
        diagnostic.message);
    check(errno == EDOM, "errno restored");
    check(semantic.rejection_count == 0u, "bootstrap admitted");
    check(semantic.function_count == 1u, "one function");
    check(semantic.functions[0].binding_count == 4u, "four actual bindings");
    check(semantic.functions[0].axis_count == 2u, "two actual iteration axes");
    return semantic;
}
static void fixture(const char *name, TsirKernelFamilyHint hint, size_t reductions) {
    size_t length;
    char *source = read_fixture(name, &length);
    TsirSemantic semantic = lower(source, length);
    check(semantic.functions[0].family_hint == hint, "inferred semantic family");
    check(semantic.functions[0].reduction_count == reductions, "reduction analysis");
    check(!strcmp(semantic.functions[0].bindings[0].name, !strcmp(name, "fused_gemv") ? "W" : !strcmp(name, "gather") ? "indices" : "input"), "source name preserved");
    /* A validated source mutation must change the source-bound identity. */
    const char *suffix = "\n// a different source identity\n";
    char *changed = malloc(length + strlen(suffix) + 1u);
    check(changed != NULL, "mutation storage");
    memcpy(changed, source, length);
    strcpy(changed + length, suffix);
    TsirSemantic mutation = lower(changed, length + strlen(suffix));
    check(memcmp(semantic.functions[0].source_digest, mutation.functions[0].source_digest, 32u) != 0, "exact immutable source bytes hashed");
    free(source);
    free(changed);
    /* Source buffers are gone; the semantic owns all its names and shapes. */
    check(!strcmp(semantic.functions[0].name, "main"), "semantic survives caller source release");
    tsir_frontend_free_semantic(&mutation);
    tsir_frontend_free_semantic(&semantic);
}
int main(void) {
    fixture("fused_gemv", TSIR_HINT_FUSED_GEMV, 1u);
    fixture("rms_norm", TSIR_HINT_REDUCTION, 1u);
    fixture("gather", TSIR_HINT_GATHER, 0u);
    DoeCompilerDiagnostic diagnostic;
    TsirSemantic semantic = {0};
    static const char invalid[] = "@compute @workgroup_size(1) fn main() { let x: u32 = true; }";
    check(tsir_source_lower(invalid, sizeof(invalid) - 1u, "v", &semantic, &diagnostic) == TSIR_ERR_INVALID, "typed invalid source rejected");
    check(diagnostic.status == DOE_COMPILER_SEMANTIC_ERROR, "semantic error retained");
    check(semantic.functions == NULL && semantic.function_count == 0u, "failure publishes no semantic");
    static const char malformed[] = "@compute @workgroup_size(1) fn main( {";
    check(tsir_source_lower(malformed, sizeof(malformed) - 1u, "v", &semantic, &diagnostic) == TSIR_ERR_INVALID, "malformed source rejected");
    check(diagnostic.message[0] != '\0', "first parser error inspectable");
    check(tsir_source_lower("x", (1u << 20) + 1u, "v", &semantic, &diagnostic) == TSIR_ERR_INVALID, "source cap before reading bytes");
    check(tsir_source_lower(NULL, 0u, "v", &semantic, &diagnostic) == TSIR_ERR_INVALID, "NULL source rejected");
    static const char nul_name[] = "@compute @workgroup_size(1) fn ma\0in() {}";
    check(tsir_source_lower(nul_name, sizeof(nul_name) - 1, "v", &semantic,
        &diagnostic) == TSIR_ERR_INVALID, "WGSL NUL identifier is not opaque TSIR text");
    static const char empty[] = "@compute @workgroup_size(1) fn main() {}";
    static const char version[] = {'v', '\0', 'x'};
    check(tsir_source_lower_span(empty, sizeof(empty) - 1, version, sizeof(version),
        &semantic, &diagnostic) == TSIR_OK, "source opaque version byte span");
    check(semantic.frontend_version_length == sizeof(version) &&
        memcmp(semantic.frontend_version, version, sizeof(version)) == 0,
        "opaque source version survives owned lowering");
    tsir_frontend_free_semantic(&semantic);
    static const char nested[] = "struct S {v:vec4f,m:mat2x2f,a:array<vec4f,2>}"
        "@compute @workgroup_size(1) fn main(){var s:S; let pair=s.v.xy;"
        "let element=s.v.z; let column=s.m[1u]; let lane=s.m[1u][0u]; let item=s.a[1u].yx;}";
    check(tsir_source_lower(nested, sizeof(nested) - 1u, "v", &semantic, &diagnostic) == TSIR_OK,
        "actual nested aggregate source projects canonical typed paths");
    check(semantic.rejection_count == 0u, "nested aggregate source has no fabricated rejection");
    tsir_frontend_free_semantic(&semantic);
    printf("TSIR source: %u checks passed\n", checks);
    return 0;
}
