/* SPDX-License-Identifier: MIT */
#include "numeric_policy.h"
#include <stdio.h>
#include <string.h>

const char *doe_ns_string(const doe_json_value *object, const char *key)
{
    const doe_json_value *value = object && object->kind == DOE_JSON_OBJECT ? doe_json_member_value(object, key) : NULL;
    return value && value->kind == DOE_JSON_STRING ? value->string : NULL;
}

static const doe_json_value *array_member(const doe_json_value *object, const char *key)
{
    const doe_json_value *value = object ? doe_json_member_value(object, key) : NULL;
    return value && value->kind == DOE_JSON_ARRAY ? value : NULL;
}

const doe_json_value *doe_ns_find(const doe_json_value *object, const char *array_key, const char *id_key, const char *id)
{
    const doe_json_value *array = array_member(object, array_key);
    for (size_t i = 0; array && id && i < array->count; i++) {
        const char *value = doe_ns_string(array->items[i], id_key);
        if (value && !strcmp(value, id)) return array->items[i];
    }
    return NULL;
}

bool doe_ns_semantic_complete(const doe_command *command)
{
    const doe_semantic *s = &command->semantic;
    return command->kind == DOE_COMMAND_KERNEL_DISPATCH && s->op_id && s->stage && s->phase;
}

static const char *basename_of(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

const doe_json_value *doe_ns_auto_detect(const doe_ns_policy *policy, const doe_command *command)
{
    if (!doe_ns_semantic_complete(command)) return NULL;
    const doe_json_value *profiles = array_member(policy->root, "autoDetectProfiles");
    const doe_semantic *s = &command->semantic;
    const char *kernel = basename_of(command->as.dispatch.kernel);
    for (size_t i = 0; profiles && i < profiles->count; i++) {
        const doe_json_value *profile = profiles->items[i];
        const doe_json_value *enabled = doe_json_member_value(profile, "enabled");
        const char *op = doe_ns_string(profile, "semanticOpId");
        const char *stage = doe_ns_string(profile, "semanticStage");
        const char *phase = doe_ns_string(profile, "semanticPhase");
        if (!enabled || enabled->kind != DOE_JSON_BOOL || !enabled->boolean || !op || strcmp(op, s->op_id)) continue;
        if ((stage && strcmp(stage, s->stage)) || (phase && strcmp(phase, s->phase))) continue;
        const doe_json_value *kernels = array_member(profile, "kernelBasenames");
        for (size_t k = 0; kernels && k < kernels->count; k++)
            if (kernels->items[k]->kind == DOE_JSON_STRING && !strcmp(kernels->items[k]->string, kernel)) return profile;
    }
    return NULL;
}

static bool require_strings(const doe_json_value *object, const char *const *keys)
{
    for (; *keys; keys++)
        if (!doe_ns_string(object, *keys)) return false;
    return true;
}

static bool each_valid(const doe_json_value *root, const char *array_key, const char *const *keys)
{
    const doe_json_value *array = array_member(root, array_key);
    if (!array || !array->count) return false;
    for (size_t i = 0; i < array->count; i++)
        if (array->items[i]->kind != DOE_JSON_OBJECT || !require_strings(array->items[i], keys)) return false;
    return true;
}

/* policy.zig ensureValidRegistry, for the members this runtime reads. */
bool doe_ns_policy_load(const char *text, size_t size, const char *path, const char *execution_profile_id,
    doe_ns_policy *policy, char *message, size_t message_size)
{
    *policy = (doe_ns_policy){.path = path};
    doe_json_error error;
    if (doe_json_parse(text, size, &policy->document, &error) != DOE_JSON_OK) {
        snprintf(message, message_size, "not JSON (%s at byte %zu)", doe_json_status_name(error.status), error.offset);
        return false;
    }
    policy->root = policy->document.root;
    uint64_t schema = 0;
    const doe_json_value *version = policy->root->kind == DOE_JSON_OBJECT ? doe_json_member_value(policy->root, "schemaVersion") : NULL;
    static const char *const top[] = {"registryVersion", "routeTaxonomyVersion", "proofArtifactPath",
        "defaultExecutionProfileId", NULL};
    static const char *const metadata[] = {"decision", "selectionMode", "committedResultMode", "downstreamAction", NULL};
    static const char *const profiles[] = {"profileId", "surface", "routingPolicyId", NULL};
    static const char *const detect[] = {"profileId", "operatorFamily", "semanticOpId", "triggerPolicyId", "routingPolicyId",
        "fastPolicyId", "stablePolicyId", "referencePolicyId", "capturePlanId", "decisionStrategyId", NULL};
    static const char *const triggers[] = {"triggerPolicyId", NULL};
    static const char *const routing[] = {"policyId", "triggerPolicyId", "triggeredDecision", "fallbackDecision", NULL};
    bool ok = version && doe_json_as_u64(version, UINT32_MAX, &schema) && schema == 3 && require_strings(policy->root, top) &&
        each_valid(policy->root, "routeDecisionMetadata", metadata) && each_valid(policy->root, "executionProfiles", profiles) &&
        each_valid(policy->root, "autoDetectProfiles", detect) && each_valid(policy->root, "triggerPolicies", triggers) &&
        each_valid(policy->root, "routingPolicies", routing) && array_member(policy->root, "routeDecisions");
    if (!ok) {
        snprintf(message, message_size, "not a schema 3 numeric stability policy registry");
        doe_ns_policy_free(policy);
        return false;
    }
    const char *profile_id = execution_profile_id ? execution_profile_id :
        doe_ns_string(policy->root, "defaultExecutionProfileId");
    policy->execution_profile = doe_ns_find(policy->root, "executionProfiles", "profileId", profile_id);
    const char *surface = doe_ns_string(policy->execution_profile, "surface");
    if (!policy->execution_profile || strcmp(surface, "ordinary-execution")) {
        snprintf(message, message_size, "execution profile %.120s is not an ordinary-execution profile of the registry",
            profile_id);
        doe_ns_policy_free(policy);
        return false;
    }
    return true;
}

void doe_ns_policy_free(doe_ns_policy *policy)
{
    doe_json_free(&policy->document);
    *policy = (doe_ns_policy){0};
}
