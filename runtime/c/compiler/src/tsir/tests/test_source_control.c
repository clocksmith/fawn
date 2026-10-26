/* SPDX-License-Identifier: MIT
 * TSIR projection controls for retained WGSL switch and continuing lowering. */
#include "../tsir_source.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
static void check(bool condition, const char *detail) {
    ++checks;
    if (!condition) { fprintf(stderr, "TSIR control: %s\n", detail); exit(1); }
}
static TsirSemantic lower(const char *source) {
    TsirSemantic result = {0};
    DoeCompilerDiagnostic diagnostic;
    TsirStatus status = tsir_source_lower(source, strlen(source), "control-test", &result, &diagnostic);
    if (status != TSIR_OK) fprintf(stderr, "lower: %s (%s)\n", diagnostic.message, tsir_status_name(status));
    check(status == TSIR_OK, "typed control source lowers");
    check(result.function_count == 1u, "one function");
    return result;
}
int main(void) {
    static const char switch_source[] =
        "@group(0) @binding(0) var<storage,read_write> output: array<u32>;"
        "@compute @workgroup_size(1) fn main() { let choice = 1u; switch choice {"
        "case 0u, 1u: { let value = 2u; output[0u] = value; }"
        "default: { let value = 3u; switch value { case 3u: { output[0u] = value; } default: {} } } } }";
    TsirSemantic semantic = lower(switch_source);
    check(semantic.functions[0].binding_count == 1u, "case-only global reference is inventoried");
    check(!strcmp(semantic.functions[0].bindings[0].name, "output"), "case binding identity preserved");
    check(semantic.rejection_count == 0u, "switch does not create spurious TSIR rejection");
    check(semantic.functions[0].axis_count == 0u, "switch adds no axes");
    tsir_frontend_free_semantic(&semantic);

    static const char continuing_source[] =
        "@group(0) @binding(0) var<storage,read_write> output: array<u32>;"
        "@compute @workgroup_size(1) fn main() { var count = 0u; loop {"
        "let threshold = 3u; count += 1u; continuing {"
        "for (var j = 0u; j < 2u; j += 1u) { output[j] = count; }"
        "break if count >= threshold; } } }";
    semantic = lower(continuing_source);
    check(semantic.functions[0].binding_count == 1u, "continuing global reference is inventoried");
    check(semantic.functions[0].axis_count == 1u, "continuing for-loop recovered exactly once");
    check(semantic.rejection_count == 1u, "bare loop retains exactly one TSIR rejection");
    check(!strcmp(semantic.rejections[0].detail, "unstructured loop"), "retained bare-loop rejection detail");
    tsir_frontend_free_semantic(&semantic);

    static const char alias_source[] =
        "@compute @workgroup_size(1) fn main() { let limit = 4u;"
        "for (var j = 0u; j < limit; j += 1u) {} }";
    semantic = lower(alias_source);
    check(semantic.functions[0].axis_count == 1u, "let-bound loop axis recovered");
    check(!strcmp(semantic.functions[0].axes[0].upper_bound, "upper_bound"), "retained TSIR does not invent a literal bound for a local alias");
    tsir_frontend_free_semantic(&semantic);

    static const char phony_source[] =
        "@group(0) @binding(0) var<storage,read> input: array<u32>;"
        "@compute @workgroup_size(1) fn main() { _ = input[0u]; }";
    semantic = lower(phony_source);
    check(semantic.functions[0].binding_count == 1u, "discarded RHS global reference is inventoried");
    check(!strcmp(semantic.functions[0].bindings[0].name, "input"), "phony assignment preserves RHS identity");
    check(semantic.rejection_count == 0u, "phony assignment is not a missing root");
    tsir_frontend_free_semantic(&semantic);

    static const char invalid[] = "@compute @workgroup_size(1) fn main() { continuing {} }";
    DoeCompilerDiagnostic diagnostic;
    check(tsir_source_lower(invalid, sizeof(invalid) - 1u, "test", &semantic, &diagnostic) == TSIR_ERR_INVALID,
        "invalid continuing still rejected by shared semantic owner");
    check(semantic.functions == NULL, "semantic failure publishes nothing");
    printf("TSIR control source: %u checks passed\n", checks);
    return 0;
}
