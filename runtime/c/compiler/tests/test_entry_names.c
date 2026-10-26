/* SPDX-License-Identifier: MIT
 * Public actual-entry metadata uses the same collision-free target spelling as
 * emitted source; full WGSL names remain selectable and reflected verbatim. */
#include "metal12_fixture.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

static void check_target(bool metal, const char *source, char names[3][81]) {
    const DoeCompilerTextCapacities caps = hlsl_capacities();
    DoeCompilerWorkspaceRequirements requirements;
    DoeCompilerStatus status = metal ?
        doe_compiler_metal_requirements(12u, &caps, sizeof(caps), &requirements, sizeof(requirements)) :
        doe_compiler_hlsl_requirements(11u, &caps, sizeof(caps), &requirements, sizeof(requirements));
    assert(status == DOE_COMPILER_OK);
    void *workspace = malloc(requirements.size_bytes);
    Metal12Fixture *fixture = calloc(1u, sizeof(*fixture));
    assert(workspace && fixture);
    for (unsigned selection_index = 0u; selection_index < 4u; ++selection_index) {
        const char *selected = selection_index ? names[selection_index - 1u] : NULL;
        DoeCompilerEntrySelectionV5 selection = {selected ? DOE_COMPILER_SELECT_NAME : DOE_COMPILER_SELECT_NONE,
            0u, selected, selected ? strlen(selected) : 0u};
        DoeCompilerTextResult result;
        DoeCompilerMetalOutputs mout = metal12_outputs(fixture);
        DoeCompilerHlslOutputs hout = hlsl_outputs(&fixture->results);
        status = metal ?
            doe_compiler_compile_metal(12u, &caps, sizeof(caps), workspace, requirements.size_bytes,
                source, strlen(source), &selection, sizeof(selection), 3u, &mout, sizeof(mout), &result, sizeof(result)) :
            doe_compiler_compile_hlsl(11u, &caps, sizeof(caps), workspace, requirements.size_bytes,
                source, strlen(source), &selection, sizeof(selection), 3u, &hout, sizeof(hout), &result, sizeof(result));
        if (status != DOE_COMPILER_OK)
            fprintf(stderr, "entry metadata status %u: %s\n", (unsigned)status, result.stages.resources.module.diagnostic.message);
        assert(status == DOE_COMPILER_OK);
        assert(result.published_entries == (selected ? 1u : 3u));
        assert(result.published_text_bytes < sizeof(fixture->results.text));
        fixture->results.text[result.published_text_bytes] = '\0';
        for (size_t i = 0u; i < result.published_entries; ++i) {
            const uint32_t index = metal ? fixture->entries[i].entry_index : fixture->results.target_entries[i].entry_index;
            const char *actual = metal ? fixture->entries[i].name : fixture->results.target_entries[i].name;
            char expected[64], declaration[96];
            assert(index < 3u);
            assert(snprintf(expected, sizeof(expected), "doe_long_entry_%u", index) > 0);
            assert(strcmp(actual, expected) == 0);
            assert(snprintf(declaration, sizeof(declaration), metal ? "host_name(\"%s\")" : "void %s(", actual) > 0);
            assert(strstr(fixture->results.text, declaration));
        }
        /* Reflection retains every declaration's full source spelling, not the
         * target spelling or its shared 63-byte prefix. */
        assert(result.stages.resources.module.published.entries == 3u);
        for (size_t i = 0u; i < 3u; ++i) {
            const DoeCompilerNameV4 span = fixture->results.entries[i].name;
            assert(span.length == strlen(names[i]));
            assert(memcmp(fixture->results.names + span.offset, names[i], span.length) == 0);
        }
    }
    free(fixture);
    free(workspace);
}
int main(void) {
    char names[3][81], source[1024];
    memset(names, 0, sizeof(names));
    memset(names[0], 'x', 80u);
    memset(names[1], 'x', 80u);
    names[0][79] = 'a'; names[1][79] = 'b';
    strcpy(names[2], "doe_long_entry_0");
    int size = snprintf(source, sizeof(source),
        "@compute @workgroup_size(1) fn %s(){}"
        "@compute @workgroup_size(1) fn %s(){}"
        "@compute @workgroup_size(1) fn %s(){}", names[0], names[1], names[2]);
    assert(size > 0 && (size_t)size < sizeof(source));
    check_target(false, source, names);
    check_target(true, source, names);
    puts("Text entry metadata: full source selection and collision-free actual target names passed");
    return 0;
}
