/* SPDX-License-Identifier: MIT */
#include "fixture.h"
#include "../../tests/stage_source.h"

static void test_mapping(void) {
    Fixture *f = prepare_graphics(target_graphics_mapping);
    char *text = emit(f, NO_ID);
    static const char *const expected[] = {"[[vertex_id]]", "[[instance_id]]", "[[attribute(2)]]",
        "[[position]]", "[[user(locn3)]] [[flat]]", "[[user(locn0)]] [[centroid_no_perspective]]",
        "[[color(0)]]", "vertex DoeStage0_2 doe_entry0(", "fragment DoeStage1_2 doe_entry1(",
        "kernel void doe_entry2("};
    for (size_t i = 0u; i < sizeof(expected) / sizeof(expected[0]); ++i) has(text, expected[i]);
    free(text);
    text = emit(f, 0u);
    assert(strstr(text, "[[color(") == NULL && strstr(text, "kernel void") == NULL);
    free(text);
    text = emit(f, 1u);
    assert(strstr(text, "[[vertex_id]]") == NULL && strstr(text, "doe_fn0(") == NULL);
    free(text);
    release(f);
}
static void test_builtins(void) {
    Fixture *f = prepare_graphics(target_fragment_builtins);
    char *text = emit(f, 0u);
    static const char *const expected[] = {"[[front_facing]]", "[[sample_id]]", "[[sample_mask]]",
        "[[position]]", "[[depth(any)]]", "[[color(0)]]", "[[sample_perspective]]"};
    for (size_t i = 0u; i < sizeof(expected) / sizeof(expected[0]); ++i) has(text, expected[i]);
    free(text);
    release(f);
    f = prepare_graphics(target_empty_fragment);
    text = emit(f, 0u);
    has(text, "fragment void doe_entry0()");
    assert(strstr(text, "struct DoeStage") == NULL);
    free(text);
    release(f);
}
static void test_failure(void) {
    Fixture *f = prepare_graphics(target_invariant_vertex);
    DoeMetalResult result;
    char untouched[8] = "intact";
    assert(doe_metal_emit(&f->source, f->module, 0u, f->scratch, untouched, sizeof(untouched), &result) ==
        DOE_COMPILER_UNSUPPORTED);
    assert(result.written_bytes == 0u && result.required_bytes == 0u);
    assert(strcmp(untouched, "intact") == 0);
    has(f->diagnostic.message, "preserve-invariance");
    release(f);
}
static void test_host_fixture(const char *path) {
    Fixture *f = prepare_graphics(target_graphics_control);
    char *text = emit(f, 0u);
    has(text, "[[color(1)]]");
    has(text, "[[sample_id]]");
    if (path != NULL) {
        FILE *file = fopen(path, "wb");
        assert(file != NULL);
        assert(fwrite(text, 1u, strlen(text), file) == strlen(text));
        assert(fclose(file) == 0);
    }
    free(text);
    release(f);
}
int main(int argc, char **argv) {
    test_mapping();
    test_builtins();
    test_failure();
    test_host_fixture(argc > 1 ? argv[1] : NULL);
    puts("Metal graphics mapping: typed stages, leaf/formal identity, attributes and output atomicity passed.");
    return 0;
}
