/* SPDX-License-Identifier: MIT
 * Independent host comparison controls; no device is opened. */
#include "oracle.h"
#include "sha256.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static doe_json_document parse_oracle(const char *text, bool accepted, doe_output_oracle *oracle)
{
    doe_json_document document;
    doe_json_error error;
    char message[256];
    assert(doe_json_parse(text, strlen(text), &document, &error) == DOE_JSON_OK);
    bool actual = doe_output_oracle_parse(document.root, oracle, message, sizeof(message));
    if (actual != accepted) fprintf(stderr, "oracle acceptance %d: %s\n", actual, message);
    assert(actual == accepted);
    return document;
}

static void set_reference(const char *path, const uint8_t *bytes, size_t size, doe_output_oracle *oracle)
{
    FILE *file = fopen(path, "wb");
    assert(file && fwrite(bytes, 1, size, file) == size && fclose(file) == 0);
    uint8_t digest[32];
    doe_sha256(bytes, size, digest);
    doe_sha256_hex(digest, oracle->expected_sha256);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    const char *prefix = "\"kind\":\"float32_reference_tolerance_v1\",\"initialization\":\"zero_fill_v1\","
        "\"binding_group\":0,\"binding\":1,\"dispatch_count\":2,\"reference_id\":\"cpu-independent\","
        "\"reference_sha256\":\"0000000000000000000000000000000000000000000000000000000000000000\","
        "\"reference_path\":\"unused\"";
    const char *fields[] = {
        "\"schema_version\":3,\"scope\":\"command_graph\",\"reference_class\":\"independent_v1\",\"absolute_tolerance\":0.001",
        "\"schema_version\":3,\"scope\":\"command_graph\",\"reference_class\":\"cross_runtime_consensus_v1\",\"relative_tolerance\":0.01",
        "\"schema_version\":3,\"scope\":\"isolated_dispatch\",\"reference_class\":\"independent_v1\",\"absolute_tolerance\":0.001",
        "\"schema_version\":3,\"scope\":\"command_graph\",\"reference_class\":\"independent_v1\"",
        "\"schema_version\":3,\"scope\":\"command_graph\",\"reference_class\":\"independent_v1\",\"absolute_tolerance\":-1",
        "\"schema_version\":3,\"scope\":\"command_graph\",\"reference_class\":\"independent_v1\",\"absolute_tolerance\":1e100",
        "\"schema_version\":3,\"scope\":\"command_graph\",\"reference_class\":\"independent_v1\",\"absolute_tolerance\":0.001,\"absoluteTolerance\":0.001",
        "\"schema_version\":2,\"scope\":\"command_graph\",\"reference_class\":\"independent_v1\",\"absolute_tolerance\":0.001",
    };
    for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) {
        char text[2048];
        snprintf(text, sizeof(text), "{%s,%s}", fields[i], prefix);
        doe_output_oracle oracle;
        doe_json_document document = parse_oracle(text, i < 2, &oracle);
        if (i < 2) {
            assert(oracle.schema_version == 3 && oracle.kind == DOE_ORACLE_FLOAT32_TOLERANCE && oracle.scope == DOE_ORACLE_COMMAND_GRAPH);
            assert(oracle.reference_class == (i == 0 ? DOE_ORACLE_INDEPENDENT : DOE_ORACLE_CONSENSUS));
        }
        doe_json_free(&document);
    }
    /* Canonical little-endian float32 values 1, -2, 0, 4. */
    uint8_t reference[] = {0,0,128,63, 0,0,0,192, 0,0,0,0, 0,0,128,64};
    uint8_t actual[sizeof(reference)];
    memcpy(actual, reference, sizeof(actual));
    doe_output_oracle oracle = {.schema_version = 3, .scope = DOE_ORACLE_COMMAND_GRAPH,
        .kind = DOE_ORACLE_FLOAT32_TOLERANCE, .reference_class = DOE_ORACLE_INDEPENDENT,
        .reference_path = argv[1], .absolute_tolerance = 0.01f, .relative_tolerance = 0.01f};
    set_reference(argv[1], reference, sizeof(reference), &oracle);
    doe_oracle_evidence evidence = {0};
    char message[256];
    bool matches;
    assert(doe_oracle_compare_bytes(&oracle, actual, sizeof(actual), &evidence, &matches, message, sizeof(message)) && matches);
    assert(evidence.compared_value_count == 4 && evidence.mismatch_count == 0 && evidence.has_actual_sha256);
    actual[0] = 100; /* Tiny real float32 perturbation: different digest, within tolerance. */
    assert(doe_oracle_compare_bytes(&oracle, actual, sizeof(actual), &evidence, &matches, message, sizeof(message)) && matches);
    assert(strcmp(evidence.actual_sha256, oracle.expected_sha256) && evidence.max_absolute_error > 0);
    actual[3] = 64; /* About 4: outside tolerance. */
    assert(doe_oracle_compare_bytes(&oracle, actual, sizeof(actual), &evidence, &matches, message, sizeof(message)) && !matches);
    assert(evidence.mismatch_count == 1);
    memcpy(actual, reference, sizeof(actual));
    actual[2] = 192; actual[3] = 127; /* NaN cannot pass. */
    assert(doe_oracle_compare_bytes(&oracle, actual, sizeof(actual), &evidence, &matches, message, sizeof(message)) && !matches);
    assert(evidence.mismatch_count == 1);
    oracle.expected_sha256[0] = oracle.expected_sha256[0] == 'a' ? 'b' : 'a';
    assert(!doe_oracle_compare_bytes(&oracle, actual, sizeof(actual), &evidence, &matches, message, sizeof(message)));
    assert(strstr(message, "digest"));
    set_reference(argv[1], reference, sizeof(reference) - 4, &oracle);
    assert(!doe_oracle_compare_bytes(&oracle, actual, sizeof(actual), &evidence, &matches, message, sizeof(message)));
    assert(strstr(message, "exactly"));
    assert(!doe_oracle_compare_bytes(&oracle, actual, sizeof(actual) - 1, &evidence, &matches, message, sizeof(message)));
    assert(remove(argv[1]) == 0);
    puts("oracle parser/class/tolerance/reference-integrity host controls passed");
    return 0;
}
