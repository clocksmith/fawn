/* SPDX-License-Identifier: MIT
 * Retained promotion order and strict policy admission controls; host-only. */
#ifndef DOE_NUMERIC_PARITY_CASES_H
#define DOE_NUMERIC_PARITY_CASES_H
#include <math.h>

static void bias_route(const char *root, const char *work)
{
    const char *json = "[{\"kind\":\"kernel_dispatch\",\"kernel\":\"bias.wgsl\","
        "\"semanticOpId\":\"matmul.logits\",\"semanticStage\":\"decode\",\"semanticPhase\":\"logits\","
        "\"numericStability\":{\"hiddenState\":{\"bufferHandle\":12,\"elementCount\":1},"
        "\"logits\":{\"bufferHandle\":13,\"elementCount\":2},"
        "\"weights\":{\"bufferHandle\":11,\"rowStrideElements\":1},"
        "\"candidates\":[{\"tokenId\":0,\"rowIndex\":0,\"bias\":0},"
        "{\"tokenId\":1,\"rowIndex\":1,\"bias\":2.98023223876953125e-8}]}}]";
    doe_stream stream;
    doe_stream_error error;
    assert(doe_stream_parse(json, strlen(json), &stream, &error));
    memory m = {0};
    put_f32(&m, 11, (const float[]){1, 1}, 2);
    put_f32(&m, 12, (const float[]){1}, 1);
    put_f32(&m, 13, (const float[]){1, 1}, 2);
    const doe_ns_io io = {.context=&m,.capture=capture,.write=write_words};
    doe_ns_evaluation evaluation = {.command=&stream.commands[0]};
    char message[256] = "", path[1024];
    assert(doe_ns_evaluate_matmul(&io, &evaluation, false, message, sizeof(message)));
    fprintf(stderr, "bias promotion: fast=%u stable=%u reference=%u stable1=%.17g\n",
        evaluation.fast_token, evaluation.stable_token, evaluation.reference_token, evaluation.candidates[1].stable);
    assert(evaluation.fast_token == 0 && evaluation.stable_token == 1 && evaluation.reference_token == 1);
    assert(evaluation.candidates[1].stable == 1.0 + 0x1p-25);
    free(evaluation.candidates);
    snprintf(path, sizeof(path), "%s/config/numeric-stability-policy.json", root);
    size_t size;
    char *text = slurp(path, &size);
    doe_ns_policy policy;
    assert(doe_ns_policy_load(text, size, path, NULL, &policy, message, sizeof(message)));
    snprintf(path, sizeof(path), "%s/test-bias-%d.meta.json", work, (int)getpid());
    doe_ns_recorder recorder;
    assert(doe_ns_recorder_open(&recorder, &policy, path));
    const doe_ns_identity identity = {.backend="host-test"};
    bool stop = false;
    assert(doe_ns_record(&recorder, &io, &identity, &stream.commands[0], NULL, 0, &stop, message, sizeof(message)));
    assert(!stop && recorder.rewrite_count == 1);
    char *receipt_path = doe_host_strdup(recorder.receipt_path);
    doe_ns_recorder_free(&recorder);
    char *receipt = slurp(receipt_path, &size);
    assert(strstr(receipt, "prefer-stable"));
    assert(strstr(receipt, "\"selectedToken\":{\"fast\":0,\"stable\":1,\"reference\":1"));
    assert(strstr(receipt, "1.0000000298023224"));
    free(receipt); remove(receipt_path); free(receipt_path);
    /* Internal defensive path still rejects nonfinite bias, not a successful receipt. */
    stream.commands[0].numeric->candidates[1].bias = INFINITY;
    evaluation = (doe_ns_evaluation){.command=&stream.commands[0]};
    message[0] = 0;
    assert(!doe_ns_evaluate_matmul(&io, &evaluation, false, message, sizeof(message)));
    assert(strstr(message, "nonfinite evaluation"));
    free(evaluation.candidates);
    doe_ns_policy_free(&policy); free(text); doe_stream_free(&stream);
}

static void policy_replaced(const char *text, const char *from, const char *to, bool accepted)
{
    const char *at = strstr(text, from);
    assert(at);
    size_t prefix = (size_t)(at - text), size = strlen(text) - strlen(from) + strlen(to);
    char *changed = malloc(size + 1);
    assert(changed);
    memcpy(changed, text, prefix);
    memcpy(changed + prefix, to, strlen(to));
    strcpy(changed + prefix + strlen(to), at + strlen(from));
    doe_ns_policy policy;
    char message[256];
    bool actual = doe_ns_policy_load(changed, size, "policy-control", NULL, &policy, message, sizeof(message));
    if (actual != accepted) fprintf(stderr, "policy mutation %s -> %s: accepted=%d\n", from, to, actual);
    assert(actual == accepted);
    doe_ns_policy_free(&policy);
    free(changed);
}

static void policy_types(const char *root)
{
    char path[1024]; size_t size;
    snprintf(path, sizeof(path), "%s/config/numeric-stability-policy.json", root);
    char *text = slurp(path, &size);
    (void)size;
    const char *flags[] = {"requireFirstDivergence", "requireSelectedTokenDisagreement",
        "requireStableMatchesExactReference", "requireFastMissesExactReference", "enabled"};
    for (size_t i = 0; i < sizeof(flags)/sizeof(flags[0]); i++) {
        char from[128], to[128];
        snprintf(from, sizeof(from), "\"%s\": true", flags[i]);
        snprintf(to, sizeof(to), "\"%s\": \"true\"", flags[i]);
        policy_replaced(text, from, to, false);
        snprintf(to, sizeof(to), "\"%s\": false", flags[i]);
        policy_replaced(text, from, to, true);
        snprintf(to, sizeof(to), "\"%s\": null", flags[i]);
        policy_replaced(text, from, to, false);
        snprintf(from, sizeof(from), "\"%s\": true,", flags[i]);
        policy_replaced(text, from, "", false);
    }
    policy_replaced(text, "\"semanticStage\": null", "\"semanticStage\": false", false);
    policy_replaced(text, "\"semanticPhase\": \"logits\"", "\"semanticPhase\": 1", false);
    policy_replaced(text, "\"semanticStage\": null,", "", true);
    policy_replaced(text, "\"semanticPhase\": \"logits\"", "\"semanticPhase\": null", true);
    policy_replaced(text, "\"proofLinks\": [", "\"unknownProof\": [", false);
    policy_replaced(text, "\"allowedSensitiveOperators\": [", "\"unknownOperators\": [", false);
    policy_replaced(text, "\"schemaVersion\": 3", "\"schemaVersion\": 3, \"unknown\": true", false);
    policy_replaced(text, "\"proofLinks\": [", "\"proofLinks\": false, \"discard\": [", false);
    policy_replaced(text, "\"theorem\":", "\"unknownTheorem\":", false);
    policy_replaced(text, "\"kernelBasenames\": [", "\"kernelBasenames\": [true,", false);
    policy_replaced(text, "\"allowedSensitiveOperators\": [", "\"allowedSensitiveOperators\": [null,", false);
    policy_replaced(text, "\"semanticStage\": null", "\"semanticStage\": \"\"", false);
    /* Canonical schema requires unique, nonempty operator/kernel strings,
     * but proof-link objects are not a set and may repeat. */
    policy_replaced(text, "\"kernelBasenames\": [", "\"kernelBasenames\": [\"matmul_logits_forward_f16accum.wgsl\",", false);
    policy_replaced(text, "\"allowedSensitiveOperators\": [", "\"allowedSensitiveOperators\": [\"matmul.logits\",", false);
    policy_replaced(text, "\"kernelBasenames\": [", "\"kernelBasenames\": [\"\",", false);
    policy_replaced(text, "\"allowedSensitiveOperators\": [", "\"allowedSensitiveOperators\": [\"\",", false);
    const char *proof = strchr(strstr(text, "\"proofLinks\""), '{');
    assert(proof);
    const char *end = strchr(proof, '}');
    assert(end);
    size_t proof_size = (size_t)(end - proof) + 1;
    char *single = malloc(proof_size + 1), *twice = malloc(proof_size * 2 + 2);
    assert(single && twice);
    memcpy(single, proof, proof_size); single[proof_size] = 0;
    memcpy(twice, single, proof_size); twice[proof_size] = ',';
    memcpy(twice + proof_size + 1, single, proof_size + 1);
    policy_replaced(text, single, twice, true);
    policy_replaced(text, single, "", false);
    free(single); free(twice);
    free(text);
}

#endif
