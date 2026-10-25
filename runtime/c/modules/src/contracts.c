/* SPDX-License-Identifier: MIT
 * Field order and defaults from retained module request/policy structs.
 * This is artifact normalization, not JSON Schema or execution policy. */
#include "module.h"

static const module_shape string = {.type = MODULE_STRING};
static const module_shape u8 = {.type = MODULE_U8};
static const module_shape u32 = {.type = MODULE_U32};
static const module_shape u64 = {.type = MODULE_U64};
static const module_shape f64 = {.type = MODULE_F64};
static const module_shape boolean = {.type = MODULE_BOOL};
static const module_shape any = {.type = MODULE_ANY};
static const module_shape strings = {.type = MODULE_ARRAY, .element = &string, .array_length = 0};
static const module_shape u32s = {.type = MODULE_ARRAY, .element = &u32, .array_length = 0};
static const module_shape doubles = {.type = MODULE_ARRAY, .element = &f64, .array_length = 0};
static const module_shape pair = {.type = MODULE_ARRAY, .element = &f64, .array_length = 2};
static const module_shape pairs = {.type = MODULE_ARRAY, .element = &pair, .array_length = 0};
static const module_shape rgba = {.type = MODULE_ARRAY, .element = &u8, .array_length = 4};
static const module_shape transform = {.type = MODULE_ARRAY, .element = &f64, .array_length = 6};
static const module_shape bounds = {.type = MODULE_ARRAY, .element = &f64, .array_length = 4};
static const module_shape maybe_bounds = {.type = MODULE_OPTIONAL, .element = &bounds};
static const module_shape maybe_string = {.type = MODULE_OPTIONAL, .element = &string};
static const module_shape maybe_f64 = {.type = MODULE_OPTIONAL, .element = &f64};

static const module_field input_fields[] = {
    {"kind", &string, NULL},
    {"bytes", &u64, NULL},
};
static const module_shape input = {.type = MODULE_OBJECT, .fields = input_fields,
    .count = sizeof(input_fields) / sizeof(input_fields[0])};
static const module_shape inputs = {.type = MODULE_ARRAY, .element = &input, .array_length = 0};

static const module_field dispatch_fields[] = {
    {"x", &u32, NULL},
    {"y", &u32, NULL},
    {"z", &u32, NULL},
};
static const module_shape dispatch = {.type = MODULE_OBJECT, .fields = dispatch_fields,
    .count = sizeof(dispatch_fields) / sizeof(dispatch_fields[0])};

static const module_field compute_options_fields[] = {
    {"safetyClass", &string, NULL},
    {"verificationMode", &string, NULL},
};
static const module_shape compute_options = {.type = MODULE_OBJECT, .fields = compute_options_fields,
    .count = sizeof(compute_options_fields) / sizeof(compute_options_fields[0])};

static const module_field compute_request_fields[] = {
    {"schemaVersion", &u32, NULL},
    {"moduleId", &string, NULL},
    {"artifactKind", &string, NULL},
    {"serviceId", &string, NULL},
    {"kernelId", &string, NULL},
    {"inputs", &inputs, NULL},
    {"dispatch", &dispatch, NULL},
    {"policy", &compute_options, NULL},
};
static const module_shape compute_request = {.type = MODULE_OBJECT, .fields = compute_request_fields,
    .count = sizeof(compute_request_fields) / sizeof(compute_request_fields[0])};

static const module_field services_fields[] = {
    {"sdf_atlas_upload", &strings, NULL},
    {"path_mask_reduce", &strings, NULL},
};
static const module_shape services = {.type = MODULE_OBJECT, .fields = services_fields,
    .count = sizeof(services_fields) / sizeof(services_fields[0])};

static const module_field compute_body_fields[] = {
    {"services", &services, NULL},
    {"maxDispatchesPerRequest", &u32, NULL},
    {"maxInputBytes", &u64, NULL},
};
static const module_shape compute_body = {.type = MODULE_OBJECT, .fields = compute_body_fields,
    .count = sizeof(compute_body_fields) / sizeof(compute_body_fields[0])};

static const module_field effect_fields[] = {
    {"nodeId", &string, NULL},
    {"op", &string, NULL},
    {"params", &any, "null"},
};
static const module_shape effect = {.type = MODULE_OBJECT, .fields = effect_fields,
    .count = sizeof(effect_fields) / sizeof(effect_fields[0])};
static const module_shape effects = {.type = MODULE_ARRAY, .element = &effect, .array_length = 0};

static const module_field surface_input_fields[] = {
    {"surfaceId", &string, NULL},
    {"bytes", &u64, NULL},
};
static const module_shape surface_input = {.type = MODULE_OBJECT, .fields = surface_input_fields,
    .count = sizeof(surface_input_fields) / sizeof(surface_input_fields[0])};
static const module_shape surface_inputs = {.type = MODULE_ARRAY, .element = &surface_input, .array_length = 0};

static const module_field effect_options_fields[] = {
    {"precisionClass", &string, NULL},
    {"colorSpace", &string, NULL},
};
static const module_shape effect_options = {.type = MODULE_OBJECT, .fields = effect_options_fields,
    .count = sizeof(effect_options_fields) / sizeof(effect_options_fields[0])};

static const module_field target_fields[] = {
    {"width", &u32, NULL},
    {"height", &u32, NULL},
};
static const module_shape target = {.type = MODULE_OBJECT, .fields = target_fields,
    .count = sizeof(target_fields) / sizeof(target_fields[0])};

static const module_field format_target_fields[] = {
    {"width", &u32, NULL},
    {"height", &u32, NULL},
    {"format", &string, NULL},
};
static const module_shape format_target = {.type = MODULE_OBJECT, .fields = format_target_fields,
    .count = sizeof(format_target_fields) / sizeof(format_target_fields[0])};

static const module_field effects_request_fields[] = {
    {"schemaVersion", &u32, NULL},
    {"moduleId", &string, NULL},
    {"artifactKind", &string, NULL},
    {"effectGraph", &effects, NULL},
    {"inputs", &surface_inputs, NULL},
    {"executionPolicy", &effect_options, NULL},
    {"target", &format_target, NULL},
};
static const module_shape effects_request = {.type = MODULE_OBJECT, .fields = effects_request_fields,
    .count = sizeof(effects_request_fields) / sizeof(effects_request_fields[0])};

static const module_field effects_body_fields[] = {
    {"allowedOps", &strings, NULL},
    {"allowedColorSpaces", &strings, NULL},
    {"maxNodeCount", &u32, NULL},
    {"maxIntermediateBytes", &u64, NULL},
};
static const module_shape effects_body = {.type = MODULE_OBJECT, .fields = effects_body_fields,
    .count = sizeof(effects_body_fields) / sizeof(effects_body_fields[0])};

static const module_field path_segment_fields[] = {
    {"segment", &string, NULL},
    {"points", &pairs, NULL},
};
static const module_shape path_segment = {.type = MODULE_OBJECT, .fields = path_segment_fields,
    .count = sizeof(path_segment_fields) / sizeof(path_segment_fields[0])};
static const module_shape path_segments = {.type = MODULE_ARRAY, .element = &path_segment, .array_length = 0};

static const module_field stroke_fields[] = {
    {"width", &f64, NULL},
    {"joinMode", &string, NULL},
    {"capMode", &string, NULL},
    {"miterLimit", &f64, NULL},
    {"dashPattern", &doubles, NULL},
};
static const module_shape stroke = {.type = MODULE_OBJECT, .fields = stroke_fields,
    .count = sizeof(stroke_fields) / sizeof(stroke_fields[0])};

static const module_field fill_fields[] = {
    {"fillRule", &string, NULL},
    {"opacity", &f64, NULL},
};
static const module_shape fill = {.type = MODULE_OBJECT, .fields = fill_fields,
    .count = sizeof(fill_fields) / sizeof(fill_fields[0])};

static const module_field clip_fields[] = {
    {"mode", &string, NULL},
};
static const module_shape clip = {.type = MODULE_OBJECT, .fields = clip_fields,
    .count = sizeof(clip_fields) / sizeof(clip_fields[0])};

static const module_field path_request_fields[] = {
    {"schemaVersion", &u32, NULL},
    {"moduleId", &string, NULL},
    {"artifactKind", &string, NULL},
    {"pathStream", &path_segments, NULL},
    {"strokeState", &stroke, NULL},
    {"fillState", &fill, NULL},
    {"clipState", &clip, NULL},
    {"target", &target, NULL},
};
static const module_shape path_request = {.type = MODULE_OBJECT, .fields = path_request_fields,
    .count = sizeof(path_request_fields) / sizeof(path_request_fields[0])};

static const module_field path_body_fields[] = {
    {"allowedJoinModes", &strings, NULL},
    {"allowDashPatterns", &boolean, NULL},
    {"maxSegments", &u32, NULL},
    {"maxTargetPixels", &u64, NULL},
};
static const module_shape path_body = {.type = MODULE_OBJECT, .fields = path_body_fields,
    .count = sizeof(path_body_fields) / sizeof(path_body_fields[0])};

static const module_field resource_fields[] = {
    {"type", &string, NULL},
    {"bytes", &u64, NULL},
    {"usage", &string, NULL},
};
static const module_shape resource = {.type = MODULE_OBJECT, .fields = resource_fields,
    .count = sizeof(resource_fields) / sizeof(resource_fields[0])};
static const module_shape resources = {.type = MODULE_ARRAY, .element = &resource, .array_length = 0};

static const module_field context_fields[] = {
    {"moduleId", &string, NULL},
    {"operationClass", &string, NULL},
};
static const module_shape context = {.type = MODULE_OBJECT, .fields = context_fields,
    .count = sizeof(context_fields) / sizeof(context_fields[0])};

static const module_field scheduler_options_fields[] = {
    {"poolLimitBytes", &u64, NULL},
    {"cadenceMode", &string, NULL},
    {"evictionPolicyMode", &string, NULL},
};
static const module_shape scheduler_options = {.type = MODULE_OBJECT, .fields = scheduler_options_fields,
    .count = sizeof(scheduler_options_fields) / sizeof(scheduler_options_fields[0])};

static const module_field profile_fields[] = {
    {"vendor", &string, NULL},
    {"api", &string, NULL},
    {"deviceFamily", &string, NULL},
    {"driver", &string, NULL},
};
static const module_shape profile = {.type = MODULE_OBJECT, .fields = profile_fields,
    .count = sizeof(profile_fields) / sizeof(profile_fields[0])};

static const module_field scheduler_request_fields[] = {
    {"schemaVersion", &u32, NULL},
    {"moduleId", &string, NULL},
    {"artifactKind", &string, NULL},
    {"resourceRequest", &resources, NULL},
    {"workloadContext", &context, NULL},
    {"schedulerPolicy", &scheduler_options, NULL},
    {"profile", &profile, NULL},
};
static const module_shape scheduler_request = {.type = MODULE_OBJECT, .fields = scheduler_request_fields,
    .count = sizeof(scheduler_request_fields) / sizeof(scheduler_request_fields[0])};

static const module_field scheduler_body_fields[] = {
    {"allowedCadenceModes", &strings, NULL},
    {"allowedModules", &strings, NULL},
    {"maxPoolBytes", &u64, NULL},
    {"maxResourcesPerRequest", &u32, NULL},
};
static const module_shape scheduler_body = {.type = MODULE_OBJECT, .fields = scheduler_body_fields,
    .count = sizeof(scheduler_body_fields) / sizeof(scheduler_body_fields[0])};

static const module_field point_fields[] = {
    {"x", &f64, NULL},
    {"y", &f64, NULL},
};
static const module_shape point = {.type = MODULE_OBJECT, .fields = point_fields,
    .count = sizeof(point_fields) / sizeof(point_fields[0])};
static const module_shape points = {.type = MODULE_ARRAY, .element = &point, .array_length = 0};

static const module_field text_run_fields[] = {
    {"fontKey", &string, NULL},
    {"fontPx", &f64, "0"},
    {"glyphIds", &u32s, NULL},
    {"positions", &points, NULL},
    {"color", &rgba, NULL},
    {"transform", &transform, NULL},
};
static const module_shape text_run = {.type = MODULE_OBJECT, .fields = text_run_fields,
    .count = sizeof(text_run_fields) / sizeof(text_run_fields[0])};
static const module_shape text_runs = {.type = MODULE_ARRAY, .element = &text_run, .array_length = 0};

static const module_field path_op_fields[] = {
    {"op", &string, NULL},
    {"points", &points, NULL},
};
static const module_shape path_op = {.type = MODULE_OBJECT, .fields = path_op_fields,
    .count = sizeof(path_op_fields) / sizeof(path_op_fields[0])};
static const module_shape path_ops = {.type = MODULE_ARRAY, .element = &path_op, .array_length = 0};

static const module_field paint_clip_fields[] = {
    {"mode", &string, NULL},
    {"bounds", &maybe_bounds, "null"},
};
static const module_shape paint_clip = {.type = MODULE_OBJECT, .fields = paint_clip_fields,
    .count = sizeof(paint_clip_fields) / sizeof(paint_clip_fields[0])};

static const module_field paint_fields[] = {
    {"blendMode", &string, NULL},
    {"clipState", &paint_clip, NULL},
    {"transform", &transform, NULL},
};
static const module_shape paint = {.type = MODULE_OBJECT, .fields = paint_fields,
    .count = sizeof(paint_fields) / sizeof(paint_fields[0])};

static const module_field sdf_target_fields[] = {
    {"width", &u32, NULL},
    {"height", &u32, NULL},
    {"format", &string, NULL},
    {"sampleCount", &u32, NULL},
};
static const module_shape sdf_target = {.type = MODULE_OBJECT, .fields = sdf_target_fields,
    .count = sizeof(sdf_target_fields) / sizeof(sdf_target_fields[0])};

static const module_field sdf_request_fields[] = {
    {"schemaVersion", &u32, NULL},
    {"moduleId", &string, NULL},
    {"artifactKind", &string, NULL},
    {"textRuns", &text_runs, NULL},
    {"pathOps", &path_ops, NULL},
    {"paintState", &paint, NULL},
    {"target", &sdf_target, NULL},
};
static const module_shape sdf_request = {.type = MODULE_OBJECT, .fields = sdf_request_fields,
    .count = sizeof(sdf_request_fields) / sizeof(sdf_request_fields[0])};

static const module_field sdf_body_fields[] = {
    {"allowedBlendModes", &strings, NULL},
    {"allowedClipModes", &strings, NULL},
    {"allowedSampleCounts", &u32s, NULL},
    {"atlasGlyphCapacity", &u32, NULL},
    {"maxPathOps", &u32, NULL},
    {"maxTargetPixels", &u64, NULL},
};
static const module_shape sdf_body = {.type = MODULE_OBJECT, .fields = sdf_body_fields,
    .count = sizeof(sdf_body_fields) / sizeof(sdf_body_fields[0])};

static const module_field compute_policy_fields[] = {
    {"schemaVersion", &u32, NULL},
    {"moduleId", &string, NULL},
    {"artifactKind", &string, NULL},
    {"version", &u32, NULL},
    {"promotedFromNursery", &boolean, NULL},
    {"promotedAt", &string, NULL},
    {"policy", &compute_body, NULL},
};
static const module_shape compute_policy = {.type = MODULE_OBJECT, .fields = compute_policy_fields,
    .count = sizeof(compute_policy_fields) / sizeof(compute_policy_fields[0])};

static const module_field effects_policy_fields[] = {
    {"schemaVersion", &u32, NULL},
    {"moduleId", &string, NULL},
    {"artifactKind", &string, NULL},
    {"version", &u32, NULL},
    {"promotedFromNursery", &boolean, NULL},
    {"promotedAt", &string, NULL},
    {"policy", &effects_body, NULL},
};
static const module_shape effects_policy = {.type = MODULE_OBJECT, .fields = effects_policy_fields,
    .count = sizeof(effects_policy_fields) / sizeof(effects_policy_fields[0])};

static const module_field path_policy_fields[] = {
    {"schemaVersion", &u32, NULL},
    {"moduleId", &string, NULL},
    {"artifactKind", &string, NULL},
    {"version", &u32, NULL},
    {"promotedFromNursery", &boolean, NULL},
    {"promotedAt", &string, NULL},
    {"policy", &path_body, NULL},
};
static const module_shape path_policy = {.type = MODULE_OBJECT, .fields = path_policy_fields,
    .count = sizeof(path_policy_fields) / sizeof(path_policy_fields[0])};

static const module_field scheduler_policy_fields[] = {
    {"schemaVersion", &u32, NULL},
    {"moduleId", &string, NULL},
    {"artifactKind", &string, NULL},
    {"version", &u32, NULL},
    {"promotedFromNursery", &boolean, NULL},
    {"promotedAt", &string, NULL},
    {"policy", &scheduler_body, NULL},
};
static const module_shape scheduler_policy = {.type = MODULE_OBJECT, .fields = scheduler_policy_fields,
    .count = sizeof(scheduler_policy_fields) / sizeof(scheduler_policy_fields[0])};

static const module_field sdf_policy_fields[] = {
    {"schemaVersion", &u32, NULL},
    {"moduleId", &string, NULL},
    {"artifactKind", &string, NULL},
    {"version", &u32, NULL},
    {"promotedFromNursery", &boolean, NULL},
    {"promotedAt", &string, NULL},
    {"policy", &sdf_body, NULL},
};
static const module_shape sdf_policy = {.type = MODULE_OBJECT, .fields = sdf_policy_fields,
    .count = sizeof(sdf_policy_fields) / sizeof(sdf_policy_fields[0])};

static const module_field candidate_fields[] = {
    {"tokenId", &u32, NULL},
    {"label", &maybe_string, "null"},
    {"weights", &doubles, NULL},
    {"bias", &maybe_f64, "null"},
};
static const module_shape candidate = {.type = MODULE_OBJECT, .fields = candidate_fields,
    .count = sizeof(candidate_fields) / sizeof(candidate_fields[0])};
static const module_shape candidates = {.type = MODULE_ARRAY, .element = &candidate, .array_length = 0};

static const module_field numeric_request_fields[] = {
    {"schemaVersion", &u32, NULL},
    {"moduleId", &string, NULL},
    {"artifactKind", &string, NULL},
    {"serviceId", &string, NULL},
    {"operatorFamily", &string, NULL},
    {"semanticOpId", &string, NULL},
    {"semanticStage", &string, NULL},
    {"semanticPhase", &string, NULL},
    {"triggerPolicyId", &string, NULL},
    {"routingPolicyId", &string, NULL},
    {"fastPolicyId", &string, NULL},
    {"stablePolicyId", &string, NULL},
    {"candidates", &candidates, NULL},
    {"hiddenState", &doubles, NULL},
    {"receiptPath", &maybe_string, "null"},
    {"traceMetaPath", &maybe_string, "null"},
};
static const module_shape numeric_request = {.type = MODULE_OBJECT, .fields = numeric_request_fields,
    .count = sizeof(numeric_request_fields) / sizeof(numeric_request_fields[0])};

static const module_field proof_fields[] = {
    {"theorem", &string, NULL},
    {"module", &string, NULL},
    {"category", &string, NULL},
    {"relation", &string, NULL},
    {"artifactPath", &string, NULL},
};
static const module_shape proof = {.type = MODULE_OBJECT, .fields = proof_fields,
    .count = sizeof(proof_fields) / sizeof(proof_fields[0])};
const module_shape module_proof_links = {.type = MODULE_ARRAY, .element = &proof, .array_length = 0};

static const module_field trigger_fields[] = {
    {"triggerPolicyId", &string, NULL},
    {"requireFirstDivergence", &boolean, NULL},
    {"requireSelectedTokenDisagreement", &boolean, NULL},
    {"requireStableMatchesExactReference", &boolean, NULL},
    {"requireFastMissesExactReference", &boolean, NULL},
    {"allowedSensitiveOperators", &strings, NULL},
    {"proofLinks", &module_proof_links, NULL},
};
static const module_shape trigger = {.type = MODULE_OBJECT, .fields = trigger_fields,
    .count = sizeof(trigger_fields) / sizeof(trigger_fields[0])};
static const module_shape triggers = {.type = MODULE_ARRAY, .element = &trigger, .array_length = 0};

static const module_field routing_fields[] = {
    {"policyId", &string, NULL},
    {"triggerPolicyId", &string, NULL},
    {"triggeredDecision", &string, NULL},
    {"fallbackDecision", &string, NULL},
    {"proofLinks", &module_proof_links, NULL},
};
static const module_shape routing = {.type = MODULE_OBJECT, .fields = routing_fields,
    .count = sizeof(routing_fields) / sizeof(routing_fields[0])};
static const module_shape routings = {.type = MODULE_ARRAY, .element = &routing, .array_length = 0};

static const module_field metadata_fields[] = {
    {"decision", &string, NULL},
    {"selectionMode", &string, NULL},
    {"committedResultMode", &string, NULL},
    {"downstreamAction", &string, NULL},
    {"proofLinks", &module_proof_links, NULL},
};
static const module_shape metadata = {.type = MODULE_OBJECT, .fields = metadata_fields,
    .count = sizeof(metadata_fields) / sizeof(metadata_fields[0])};
static const module_shape metadatas = {.type = MODULE_ARRAY, .element = &metadata, .array_length = 0};

static const module_field auto_profile_fields[] = {
    {"profileId", &string, NULL},
    {"enabled", &boolean, NULL},
    {"operatorFamily", &string, NULL},
    {"semanticOpId", &string, NULL},
    {"semanticStage", &maybe_string, "null"},
    {"semanticPhase", &maybe_string, "null"},
    {"kernelBasenames", &strings, NULL},
    {"triggerPolicyId", &string, NULL},
    {"routingPolicyId", &string, NULL},
    {"fastPolicyId", &string, NULL},
    {"stablePolicyId", &string, NULL},
    {"referencePolicyId", &string, NULL},
    {"capturePlanId", &string, NULL},
    {"decisionStrategyId", &string, NULL},
};
static const module_shape auto_profile = {.type = MODULE_OBJECT, .fields = auto_profile_fields,
    .count = sizeof(auto_profile_fields) / sizeof(auto_profile_fields[0])};
static const module_shape auto_profiles = {.type = MODULE_ARRAY, .element = &auto_profile, .array_length = 0};

static const module_field execution_profile_fields[] = {
    {"profileId", &string, NULL},
    {"surface", &string, NULL},
    {"description", &string, NULL},
    {"routingPolicyId", &string, NULL},
};
static const module_shape execution_profile = {.type = MODULE_OBJECT, .fields = execution_profile_fields,
    .count = sizeof(execution_profile_fields) / sizeof(execution_profile_fields[0])};
static const module_shape execution_profiles = {.type = MODULE_ARRAY, .element = &execution_profile, .array_length = 0};

static const module_field module_numeric_registry_fields[] = {
    {"schemaVersion", &u32, NULL},
    {"registryVersion", &string, NULL},
    {"routeTaxonomyVersion", &string, NULL},
    {"proofArtifactPath", &string, NULL},
    {"defaultExecutionProfileId", &string, NULL},
    {"routeDecisions", &strings, NULL},
    {"routeDecisionMetadata", &metadatas, NULL},
    {"executionProfiles", &execution_profiles, NULL},
    {"autoDetectProfiles", &auto_profiles, NULL},
    {"triggerPolicies", &triggers, NULL},
    {"routingPolicies", &routings, NULL},
};
const module_shape module_numeric_registry = {.type = MODULE_OBJECT, .fields = module_numeric_registry_fields,
    .count = sizeof(module_numeric_registry_fields) / sizeof(module_numeric_registry_fields[0])};

const module_contract module_contracts[6] = {
    {"fawn_compute_services", MODULE_COMPUTE, &compute_request, &compute_policy},
    {"fawn_effects_pipeline", MODULE_EFFECTS, &effects_request, &effects_policy},
    {"fawn_path_engine", MODULE_PATH, &path_request, &path_policy},
    {"fawn_resource_scheduler", MODULE_SCHEDULER, &scheduler_request, &scheduler_policy},
    {"fawn_2d_sdf_renderer", MODULE_SDF, &sdf_request, &sdf_policy},
    {"doe_numeric_stability", MODULE_NUMERIC, &numeric_request, &module_numeric_registry},
};
