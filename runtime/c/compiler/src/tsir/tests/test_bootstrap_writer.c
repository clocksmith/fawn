/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "tsir.h"
#include "tsir_json.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    static const char hash[] =
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    TsirManifestEntry entry;
    TsirBuffer output;
    tsir_buf_init(&output);
    assert(TSIR_CONTRACT_VERSION == 1u);
    assert(tsir_bootstrap_manifest_template("fused_gemv", "msl", hash,
        hash, hash, hash, &entry) == TSIR_OK);
    tsir_buf_puts(&output, "prefix");
    entry.algorithm_exact_invariants = NULL;
    assert(tsir_bootstrap_write_manifest_entry(&output, &entry) == TSIR_ERR_INVALID);
    assert(output.len == 6 && strcmp(output.data, "prefix") == 0);
    assert(tsir_bootstrap_manifest_template("gather", "msl", hash,
        hash, hash, hash, &entry) == TSIR_OK);
    entry.tolerance_epsilon = "0, \"injected\": true";
    assert(tsir_bootstrap_write_manifest_entry(&output, &entry) == TSIR_ERR_INVALID);
    assert(strcmp(output.data, "prefix") == 0);
    entry.tolerance_epsilon = "null";
    assert(tsir_bootstrap_write_manifest_entry(&output, &entry) == TSIR_ERR_INVALID);
    tsir_buf_free(&output);
    entry.tolerance_epsilon = "0";
    entry.kernel_ref = "x\"\n\001";
    entry.backend = "msl\002";
    assert(tsir_bootstrap_write_manifest_entry(&output, &entry) == TSIR_OK);
    doe_json_document doc = {0};
    assert(doe_json_parse(output.data, output.len, &doc, NULL) == DOE_JSON_OK);
    const doe_json_value *kernel = doe_json_member_value(doc.root, "kernelRef");
    assert(kernel && strcmp(kernel->string, "doe.tsir.bootstrap.x\"\n\001") == 0);
    doe_json_free(&doc);
    output.err = TSIR_ERR_RANGE;
    size_t prior = output.len;
    assert(tsir_bootstrap_write_manifest_entry(&output, &entry) == TSIR_ERR_RANGE);
    assert(output.len == prior);
    tsir_buf_free(&output);
    puts("TSIR facade/writer: invalid arrays, JSON injection, escaping, atomic publication and first error passed");
    return 0;
}
