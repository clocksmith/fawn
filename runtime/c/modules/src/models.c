/* SPDX-License-Identifier: MIT
 * Host policy models retained from full/modules/{services,rendering}.
 * Timing fields are deterministic model predictions, not device measurements. */
#include "module.h"
#include <inttypes.h>
#include <string.h>

static bool histogram(FILE *out, const char *field, const char *const *names, const uint32_t *counts, size_t length)
{
    uint64_t total = 0;
    for (size_t i = 0; i < length; i++) total += counts[i];
    if (fprintf(out, ",\"%s\":{\"fallbackCount\":%" PRIu64 ",\"fallbackReasonHistogram\":{", field, total) < 0) return false;
    for (size_t i = 0; i < length; i++)
        if (fprintf(out, "%s\"%s\":%" PRIu32, i ? "," : "", names[i], counts[i]) < 0) return false;
    return fputs("}}", out) >= 0;
}

static bool compute(FILE *out, const doe_json_value *r, const doe_json_value *p)
{
    const doe_json_value *dispatch = module_get(r, "dispatch"), *inputs = module_get(r, "inputs");
    uint64_t x = module_uint(dispatch, "x"), y = module_uint(dispatch, "y"), z = module_uint(dispatch, "z");
    uint64_t count, bytes, setup, encode, submit, timing;
    if (!x || !y || !z || !module_mul(x, y, &count) || !module_mul(count, z, &count) ||
        !module_sum(inputs, "bytes", &bytes) || !module_mul(inputs->count, 300, &setup) ||
        !module_add(setup, 6000, &setup) || !module_mul(count, 2, &encode) || !module_add(encode, 5000, &encode) ||
        !module_add(count, 3000, &submit) || !module_mul(count, 3, &timing) || !module_add(timing, 4000, &timing)) return false;
    const char *failure = "none", *status = "ok";
    if (bytes > module_uint(p, "maxInputBytes")) failure = "input_contract_invalid";
    else if (count > module_uint(p, "maxDispatchesPerRequest")) failure = "dispatch_contract_invalid";
    if (strcmp(failure, "none")) status = "fallback";
    else {
        const doe_json_value *service = module_get(r, "serviceId"), *kernels = NULL;
        if (module_equal(service, "sdf_atlas_upload")) kernels = module_get(module_get(p, "services"), "sdf_atlas_upload");
        else if (module_equal(service, "path_mask_reduce")) kernels = module_get(module_get(p, "services"), "path_mask_reduce");
        if (!kernels) failure = "service_id_unknown";
        else if (!module_contains(kernels, module_get(r, "kernelId"))) failure = "kernel_id_unknown";
        if (strcmp(failure, "none")) status = "error";
    }
    return fprintf(out, ",\"serviceResult\":{\"status\":\"%s\"},\"executionStats\":{\"dispatchCount\":%" PRIu64
        ",\"bytesMoved\":%" PRIu64 "},\"timingStats\":{\"setupNs\":%" PRIu64 ",\"encodeNs\":%" PRIu64
        ",\"submitNs\":%" PRIu64 ",\"dispatchNs\":%" PRIu64 "},\"failureDetails\":{\"code\":\"%s\"}",
        status, count, bytes, setup, encode, submit, timing, failure) >= 0;
}

static bool effects(FILE *out, const doe_json_value *r, const doe_json_value *p, const char hash[65])
{
    static const char *const names[] = {"effect_op_unsupported", "color_space_mode_unsupported", "intermediate_budget_exceeded",
        "runtime_unavailable", "runtime_execute_failed"};
    uint32_t failures[5] = {0};
    const doe_json_value *nodes = module_get(r, "effectGraph");
    uint64_t count = nodes->count, passes = count ? count : 1, bytes;
    if (count > UINT32_MAX || !module_sum(module_get(r, "inputs"), "bytes", &bytes) ||
        !module_mul(bytes, passes, &bytes)) return false;
    if (count > module_uint(p, "maxNodeCount")) failures[0]++;
    if (!module_contains(module_get(p, "allowedColorSpaces"), module_get(module_get(r, "executionPolicy"), "colorSpace")))
        failures[1]++;
    for (size_t i = 0; i < nodes->count; i++) {
        if (!module_contains(module_get(p, "allowedOps"), module_get(nodes->items[i], "op"))) {
            failures[0]++;
            break;
        }
    }
    if (bytes > module_uint(p, "maxIntermediateBytes")) failures[2]++;
    uint64_t effective = failures[0] || failures[1] || failures[2] ? 0 : passes;
    return fprintf(out, ",\"outputArtifact\":{\"artifactId\":\"effects://%.16s\"},\"executionStats\":{\"nodeCount\":%" PRIu64
        ",\"passCount\":%" PRIu64 ",\"temporaryBytes\":%" PRIu64 "},\"timingStats\":{\"setupNs\":%" PRIu64
        ",\"encodeNs\":%" PRIu64 ",\"submitWaitNs\":%" PRIu64 "}", hash, count, effective, bytes,
        8000 + count * 400, 7000 + passes * 700, 3000 + passes * 500) >= 0 &&
        histogram(out, "fallbackStats", names, failures, 5);
}

static bool path(FILE *out, const doe_json_value *r, const doe_json_value *p)
{
    static const char *const names[] = {"join_mode_unsupported", "dash_pattern_unsupported", "geometry_pathological",
        "resource_budget_exceeded", "runtime_unavailable", "runtime_execute_failed"};
    uint32_t failures[6] = {0};
    uint64_t segments = module_get(r, "pathStream")->count;
    if (segments > UINT32_MAX / 2) return false;
    const doe_json_value *stroke = module_get(r, "strokeState"), *target = module_get(r, "target");
    uint64_t pixels = module_uint(target, "width") * module_uint(target, "height");
    failures[0] = !module_contains(module_get(p, "allowedJoinModes"), module_get(stroke, "joinMode"));
    failures[1] = module_get(stroke, "dashPattern")->count && !module_get(p, "allowDashPatterns")->boolean;
    failures[2] = segments > module_uint(p, "maxSegments");
    failures[3] = pixels > module_uint(p, "maxTargetPixels");
    bool failed = failures[0] || failures[1] || failures[2] || failures[3];
    uint64_t draws = segments ? (segments + 1) / 2 : 1;
    return fprintf(out, ",\"geometryStats\":{\"segmentCount\":%" PRIu64 ",\"tessellatedPrimitiveCount\":%" PRIu64
        "},\"rasterStats\":{\"passCount\":%u,\"drawCallCount\":%" PRIu64 "}", segments, segments * 2,
        failed ? 0u : 1u, failed ? 0 : draws) >= 0 && histogram(out, "fallbackStats", names, failures, 6);
}

static bool scheduler(FILE *out, const doe_json_value *r, const doe_json_value *p)
{
    static const char *const names[] = {"cadence_policy_invalid", "profile_policy_missing", "determinism_guard_triggered",
        "pool_limit_exceeded", "resource_create_failed"};
    uint32_t failures[5] = {0};
    const doe_json_value *resources = module_get(r, "resourceRequest"), *options = module_get(r, "schedulerPolicy");
    const doe_json_value *cadence = module_get(options, "cadenceMode");
    uint64_t count = resources->count, requested, used = 0, pool = module_uint(options, "poolLimitBytes");
    if (count >= UINT32_MAX || !module_sum(resources, "bytes", &requested)) return false;
    failures[0] = !module_contains(module_get(p, "allowedCadenceModes"), cadence);
    failures[1] = !module_contains(module_get(p, "allowedModules"), module_get(module_get(r, "workloadContext"), "moduleId"));
    failures[2] = count > module_uint(p, "maxResourcesPerRequest");
    uint64_t evictions = failures[2] ? count - module_uint(p, "maxResourcesPerRequest") : 0;
    if (pool > module_uint(p, "maxPoolBytes")) {
        pool = module_uint(p, "maxPoolBytes");
        failures[3]++;
    }
    if (fputs(",\"allocationResult\":[", out) < 0) return false;
    for (size_t i = 0; i < resources->count; i++) {
        const char *disposition = i % 2 ? "allocated" : "reused";
        uint64_t granted = module_uint(resources->items[i], "bytes");
        if (granted > pool - used) {
            granted = pool - used;
            disposition = "fallback";
            failures[3]++;
        }
        used += granted;
        if (fprintf(out, "%s{\"resourceId\":\"res_%zu\",\"disposition\":\"%s\",\"bytesGranted\":%" PRIu64 "}",
            i ? "," : "", i, disposition, granted) < 0) return false;
    }
    return fprintf(out, "],\"poolStats\":{\"hitCount\":%" PRIu64 ",\"missCount\":%" PRIu64 ",\"evictionCount\":%" PRIu64
        ",\"highWaterBytes\":%" PRIu64 "},\"submitStats\":{\"submitCount\":%" PRIu64 ",\"cadenceModeUsed\":",
        (count + 1) / 2, count / 2, evictions, requested < pool ? requested : pool, count / 2 ? count / 2 : 1) >= 0 &&
        module_json(out, cadence) && fputc('}', out) != EOF && histogram(out, "fallbackStats", names, failures, 5);
}

static bool sdf(FILE *out, const doe_json_value *r, const doe_json_value *p, const char hash[65])
{
    static const char *const names[] = {"unsupported_blend_mode", "unsupported_clip_mode", "required_capability_missing",
        "path_complexity_exceeded", "resource_budget_exceeded", "runtime_unavailable", "runtime_execute_failed"};
    uint32_t failures[7] = {0};
    const doe_json_value *runs = module_get(r, "textRuns"), *paint = module_get(r, "paintState"), *target = module_get(r, "target");
    uint64_t paths = module_get(r, "pathOps")->count, glyphs = 0;
    for (size_t i = 0; i < runs->count; i++)
        if (!module_add(glyphs, module_get(runs->items[i], "glyphIds")->count, &glyphs)) return false;
    uint64_t draws = runs->count + (paths ? (paths + 1) / 2 : 1), capacity = module_uint(p, "atlasGlyphCapacity");
    if (paths > UINT32_MAX || glyphs > UINT32_MAX || draws > UINT32_MAX) return false;
    uint64_t misses = glyphs > capacity ? glyphs - capacity : 0;
    failures[0] = !module_contains(module_get(p, "allowedBlendModes"), module_get(paint, "blendMode"));
    failures[1] = !module_contains(module_get(p, "allowedClipModes"), module_get(module_get(paint, "clipState"), "mode"));
    failures[2] = !module_contains(module_get(p, "allowedSampleCounts"), module_get(target, "sampleCount"));
    failures[3] = paths > module_uint(p, "maxPathOps");
    failures[4] = module_uint(target, "width") * module_uint(target, "height") > module_uint(p, "maxTargetPixels");
    bool failed = failures[0] || failures[1] || failures[2] || failures[3] || failures[4];
    return fprintf(out, ",\"renderArtifact\":{\"artifactId\":\"sdf://%.16s\"},\"renderStats\":{\"drawCallCount\":%" PRIu64
        ",\"atlasHitCount\":%" PRIu64 ",\"atlasMissCount\":%" PRIu64 ",\"passCount\":%u},\"timingStats\":{\"setupNs\":%" PRIu64
        ",\"encodeNs\":%" PRIu64 ",\"submitWaitNs\":%" PRIu64 "}", hash, failed ? 0 : draws, glyphs - misses, misses,
        failed ? 0u : 1u, 10000 + glyphs * 50 + paths * 30, 5000 + draws * 120,
        2000 + module_uint(target, "sampleCount") * 500) >= 0 && histogram(out, "qualityStats", names, failures, 7);
}

bool module_model(FILE *out, const module_contract *c, const doe_json_value *r, const doe_json_value *policy,
    const char request_hash[65])
{
    const doe_json_value *p = module_get(policy, "policy");
    switch (c->kind) {
    case MODULE_COMPUTE: return compute(out, r, p);
    case MODULE_EFFECTS: return effects(out, r, p, request_hash);
    case MODULE_PATH: return path(out, r, p);
    case MODULE_SCHEDULER: return scheduler(out, r, p);
    case MODULE_SDF: return sdf(out, r, p, request_hash);
    case MODULE_NUMERIC: return false;
    }
    return false;
}
