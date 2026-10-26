/* SPDX-License-Identifier: MIT */
#include "numeric_policy.h"
#include "host_io.h"
#include <stdio.h>
#include <string.h>

const char *doe_ns_string(const doe_json_value *object, const char *key)
{
    const doe_json_value *value = object && object->kind == DOE_JSON_OBJECT ? doe_json_member_value(object, key) : NULL;
    return value && value->kind == DOE_JSON_STRING && value->string_size == strlen(value->string) ? value->string : NULL;
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
    return doe_host_basename(path);
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

static bool contains_text(const doe_json_value *array, const char *text)
{
    for (size_t i = 0; array && text && i < array->count; i++)
        if (array->items[i]->kind == DOE_JSON_STRING && !strcmp(array->items[i]->string, text)) return true;
    return false;
}

static bool nonempty_strings(const doe_json_value *object, const char *const *keys)
{
    for (; *keys; keys++) {
        const char *value = doe_ns_string(object, *keys);
        if (!value || !*value) return false;
    }
    return true;
}

/* Typed schema admission belongs to policy loading, before a flag can influence
 * route selection. Optional stage/phase may be absent or null, never mistyped. */
typedef enum policy_field_kind { POLICY_TEXT, POLICY_BOOL, POLICY_TEXTS, POLICY_PROOFS,
    POLICY_OPTIONAL_TEXT } policy_field_kind;
typedef struct policy_field { const char *name; policy_field_kind kind; } policy_field;
static bool policy_object(const doe_json_value *, const policy_field *, size_t);
static bool plain_text(const doe_json_value *value)
{
    return value && value->kind == DOE_JSON_STRING && value->string_size &&
        value->string_size == strlen(value->string);
}
static bool text_array(const doe_json_value *value)
{
    if (!value || value->kind != DOE_JSON_ARRAY || !value->count) return false;
    for (size_t i = 0; i < value->count; i++) {
        if (!plain_text(value->items[i])) return false;
        for (size_t j = 0; j < i; j++)
            if (!strcmp(value->items[i]->string, value->items[j]->string)) return false;
    }
    return true;
}
static bool proof_array(const doe_json_value *value)
{
    static const policy_field fields[] = {{"theorem",POLICY_TEXT},{"module",POLICY_TEXT},
        {"category",POLICY_TEXT},{"relation",POLICY_TEXT},{"artifactPath",POLICY_TEXT}};
    if (!value || value->kind != DOE_JSON_ARRAY || !value->count) return false;
    for (size_t i = 0; i < value->count; i++)
        if (!policy_object(value->items[i], fields, sizeof(fields)/sizeof(fields[0]))) return false;
    return true;
}
static bool policy_object(const doe_json_value *object, const policy_field *fields, size_t count)
{
    if (!object || object->kind != DOE_JSON_OBJECT) return false;
    for (size_t i = 0; i < object->count; i++) {
        const doe_json_member *member = &object->members[i];
        bool known = false;
        for (size_t j = 0; j < count; j++)
            if (member->key_size == strlen(fields[j].name) && !memcmp(member->key, fields[j].name, member->key_size)) known = true;
        if (!known) return false;
    }
    for (size_t i = 0; i < count; i++) {
        const doe_json_value *value = doe_json_member_value(object, fields[i].name);
        switch (fields[i].kind) {
            case POLICY_TEXT: if (!plain_text(value)) return false; break;
            case POLICY_BOOL: if (!value || value->kind != DOE_JSON_BOOL) return false; break;
            case POLICY_TEXTS: if (!text_array(value)) return false; break;
            case POLICY_PROOFS: if (!proof_array(value)) return false; break;
            case POLICY_OPTIONAL_TEXT:
                if (value && value->kind != DOE_JSON_NULL && !plain_text(value)) return false;
                break;
        }
    }
    return true;
}
static bool typed_entries(const doe_json_value *root, const char *key, const policy_field *fields, size_t count)
{
    const doe_json_value *array = array_member(root, key);
    if (!array || !array->count) return false;
    for (size_t i = 0; i < array->count; i++)
        if (!policy_object(array->items[i], fields, count)) return false;
    return true;
}
static bool registry_types_valid(const doe_json_value *root)
{
    static const char *const root_keys[] = {"schemaVersion", "registryVersion", "routeTaxonomyVersion", "proofArtifactPath",
        "defaultExecutionProfileId", "routeDecisions", "routeDecisionMetadata", "executionProfiles", "autoDetectProfiles",
        "triggerPolicies", "routingPolicies"};
    if (!root || root->kind != DOE_JSON_OBJECT || root->count != sizeof(root_keys)/sizeof(root_keys[0])) return false;
    for (size_t i = 0; i < root->count; i++) {
        bool known = false;
        for (size_t j = 0; j < sizeof(root_keys)/sizeof(root_keys[0]); j++)
            if (root->members[i].key_size == strlen(root_keys[j]) && !memcmp(root->members[i].key, root_keys[j], root->members[i].key_size)) known = true;
        if (!known) return false;
    }
    static const policy_field triggers[] = {{"triggerPolicyId",POLICY_TEXT},
        {"requireFirstDivergence",POLICY_BOOL},{"requireSelectedTokenDisagreement",POLICY_BOOL},
        {"requireStableMatchesExactReference",POLICY_BOOL},{"requireFastMissesExactReference",POLICY_BOOL},
        {"allowedSensitiveOperators",POLICY_TEXTS},{"proofLinks",POLICY_PROOFS}};
    static const policy_field routing[] = {{"policyId",POLICY_TEXT},{"triggerPolicyId",POLICY_TEXT},
        {"triggeredDecision",POLICY_TEXT},{"fallbackDecision",POLICY_TEXT},{"proofLinks",POLICY_PROOFS}};
    static const policy_field metadata[] = {{"decision",POLICY_TEXT},{"selectionMode",POLICY_TEXT},
        {"committedResultMode",POLICY_TEXT},{"downstreamAction",POLICY_TEXT},{"proofLinks",POLICY_PROOFS}};
    static const policy_field profiles[] = {{"profileId",POLICY_TEXT},{"surface",POLICY_TEXT},
        {"description",POLICY_TEXT},{"routingPolicyId",POLICY_TEXT}};
    static const policy_field detect[] = {{"profileId",POLICY_TEXT},{"enabled",POLICY_BOOL},
        {"operatorFamily",POLICY_TEXT},{"semanticOpId",POLICY_TEXT},{"semanticStage",POLICY_OPTIONAL_TEXT},
        {"semanticPhase",POLICY_OPTIONAL_TEXT},{"kernelBasenames",POLICY_TEXTS},{"triggerPolicyId",POLICY_TEXT},
        {"routingPolicyId",POLICY_TEXT},{"fastPolicyId",POLICY_TEXT},{"stablePolicyId",POLICY_TEXT},
        {"referencePolicyId",POLICY_TEXT},{"capturePlanId",POLICY_TEXT},{"decisionStrategyId",POLICY_TEXT}};
    return text_array(doe_json_member_value(root, "routeDecisions")) &&
        typed_entries(root,"triggerPolicies",triggers,sizeof(triggers)/sizeof(triggers[0])) &&
        typed_entries(root,"routingPolicies",routing,sizeof(routing)/sizeof(routing[0])) &&
        typed_entries(root,"routeDecisionMetadata",metadata,sizeof(metadata)/sizeof(metadata[0])) &&
        typed_entries(root,"executionProfiles",profiles,sizeof(profiles)/sizeof(profiles[0])) &&
        typed_entries(root,"autoDetectProfiles",detect,sizeof(detect)/sizeof(detect[0]));
}

/* policy.zig ensureValidRegistry: all references and route semantics, shared by
 * ordinary execution and the host module service. */
static bool registry_links_valid(const doe_json_value *root)
{
    const doe_json_value *decisions = array_member(root, "routeDecisions");
    if (!decisions || !decisions->count) return false;
    static const char *const top[] = {"registryVersion", "routeTaxonomyVersion", "proofArtifactPath",
        "defaultExecutionProfileId", NULL};
    if (!nonempty_strings(root, top)) return false;
    const doe_json_value *metadata = array_member(root, "routeDecisionMetadata");
    for (size_t i = 0; i < metadata->count; i++) {
        const doe_json_value *entry = metadata->items[i];
        const char *selection = doe_ns_string(entry, "selectionMode"), *committed = doe_ns_string(entry, "committedResultMode");
        const char *action = doe_ns_string(entry, "downstreamAction");
        if (!contains_text(decisions, doe_ns_string(entry, "decision")) ||
            (strcmp(selection, "fast") && strcmp(selection, "stable") && strcmp(selection, "none")) ||
            (strcmp(committed, "fast") && strcmp(committed, "stable") && strcmp(committed, "none")) ||
            (strcmp(action, "continue") && strcmp(action, "stop"))) return false;
    }
    for (size_t i = 0; i < decisions->count; i++)
        if (decisions->items[i]->kind != DOE_JSON_STRING ||
            !doe_ns_find(root, "routeDecisionMetadata", "decision", decisions->items[i]->string)) return false;
    const doe_json_value *profiles = array_member(root, "executionProfiles");
    static const char *const profile_keys[] = {"profileId", "description", NULL};
    for (size_t i = 0; i < profiles->count; i++) {
        const doe_json_value *entry = profiles->items[i];
        if (!nonempty_strings(entry, profile_keys) || strcmp(doe_ns_string(entry, "surface"), "ordinary-execution") ||
            !doe_ns_find(root, "routingPolicies", "policyId", doe_ns_string(entry, "routingPolicyId"))) return false;
    }
    if (!doe_ns_find(root, "executionProfiles", "profileId", doe_ns_string(root, "defaultExecutionProfileId"))) return false;
    const doe_json_value *routing = array_member(root, "routingPolicies");
    for (size_t i = 0; i < routing->count; i++) {
        const doe_json_value *entry = routing->items[i];
        if (!contains_text(decisions, doe_ns_string(entry, "triggeredDecision")) ||
            !contains_text(decisions, doe_ns_string(entry, "fallbackDecision")) ||
            !doe_ns_find(root, "triggerPolicies", "triggerPolicyId", doe_ns_string(entry, "triggerPolicyId"))) return false;
    }
    static const char *const detect_keys[] = {"profileId", "operatorFamily", "semanticOpId", "fastPolicyId", "stablePolicyId",
        "referencePolicyId", "capturePlanId", "decisionStrategyId", NULL};
    const doe_json_value *detect = array_member(root, "autoDetectProfiles");
    for (size_t i = 0; i < detect->count; i++) {
        const doe_json_value *entry = detect->items[i], *kernels = array_member(entry, "kernelBasenames");
        const char *trigger_id = doe_ns_string(entry, "triggerPolicyId");
        const doe_json_value *route = doe_ns_find(root, "routingPolicies", "policyId", doe_ns_string(entry, "routingPolicyId"));
        if (!nonempty_strings(entry, detect_keys) || !kernels || !kernels->count || !route ||
            strcmp(doe_ns_string(route, "triggerPolicyId"), trigger_id) ||
            !doe_ns_find(root, "triggerPolicies", "triggerPolicyId", trigger_id)) return false;
    }
    return true;
}

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
    bool ok = registry_types_valid(policy->root) && version &&
        doe_json_as_u64(version, UINT32_MAX, &schema) && schema == 3 && registry_links_valid(policy->root);
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

bool doe_ns_capture_plan_supported(const char *plan)
{
    return plan && (!strcmp(plan, DOE_NS_RMSNORM_PLAN) || !strcmp(plan, DOE_NS_ATTENTION_PLAN) || !strcmp(plan, DOE_NS_DECODE_FINAL_LOGITS_PLAN) || !strcmp(plan, DOE_NS_MATMUL_LOGITS_PLAN));
}
