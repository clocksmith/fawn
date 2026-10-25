/* SPDX-License-Identifier: MIT */
#include "quirks.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* doe-zig-runtime's buckets for the kinds this runtime executes: buffer_write
 * shares upload's, render_pass shares render_draw's. */
typedef enum bucket_kind {
    BUCKET_UPLOAD, BUCKET_COPY, BUCKET_BARRIER, BUCKET_KERNEL_DISPATCH, BUCKET_RENDER_DRAW,
    BUCKET_SAMPLER_CREATE, BUCKET_SAMPLER_DESTROY, BUCKET_TEXTURE_WRITE, BUCKET_TEXTURE_QUERY,
    BUCKET_TEXTURE_DESTROY, BUCKET_DISPATCH, BUCKET_COUNT
} bucket_kind;
_Static_assert((int)BUCKET_COUNT <= (int)DOE_QUIRK_BUCKETS, "bucket storage");

enum { STAGING_TOGGLE_ALIGNMENT = 256, SCHEMA_VERSION = 2 };
static const char *const SCOPES[] = {"alignment", "barrier", "layout", "driver_toggle", "memory", NULL};
static const char *const SAFETY[] = {"low", "moderate", "high", "critical", NULL};
static const char *const VERIFICATION[] = {"guard_only", "lean_preferred", "lean_required", NULL};
static const char *const PROOF[] = {"proven", "guarded", "rejected", NULL};
static const char *const APIS[] = {"vulkan", "metal", "d3d12", "webgpu", NULL};
static const char *const ACTIONS[] = {"use_temporary_buffer", "use_temporary_render_texture", "toggle", "no_op", NULL};
/* Behavioral toggles that route copies through a staging buffer (quirk_actions.zig). */
static const char *const STAGING_TOGGLES[] = {"use_temporary_buffer_in_texture_to_texture_copy",
    "use_temp_buffer_in_small_format_texture_to_texture_copy_from_greater_to_less_mip_level",
    "d3d12_use_temp_buffer_in_depth_stencil_texture_and_buffer_copy_with_non_zero_buffer_offset",
    "d3d12_use_temp_buffer_in_texture_to_texture_copy_between_different_dimensions", NULL};

doe_toggle_effect doe_toggle_effect_of(const char *name)
{
    for (size_t i = 0; i < DOE_TOGGLE_REGISTRY_COUNT; i++)
        if (!strcasecmp(name, DOE_TOGGLE_REGISTRY[i].name)) return DOE_TOGGLE_REGISTRY[i].effect;
    return DOE_TOGGLE_UNHANDLED;
}

static bool is_staging_toggle(const char *name)
{
    for (const char *const *known = STAGING_TOGGLES; *known; known++) if (!strcasecmp(name, *known)) return true;
    return false;
}

static int lookup(const char *const *names, const char *value)
{
    for (int i = 0; names[i]; i++) if (!strcasecmp(names[i], value)) return i;
    return -1;
}

bool doe_semver_parse(const char *text, uint32_t out[3])
{
    out[0] = out[1] = out[2] = 0;
    int part = 0;
    const char *cursor = text;
    for (;;) {
        if (part == 3 || *cursor < '0' || *cursor > '9') return false;
        uint64_t value = 0;
        while (*cursor >= '0' && *cursor <= '9') {
            value = value * 10 + (uint64_t)(*cursor++ - '0');
            if (value > UINT32_MAX) return false;
        }
        out[part++] = (uint32_t)value;
        if (*cursor == '\0') return true;
        if (*cursor++ != '.') return false;
    }
}

static int semver_cmp(const uint32_t a[3], const uint32_t b[3])
{
    for (int i = 0; i < 3; i++) if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    return 0;
}

/* ">=a,<b" and the other comparison tokens of quirk driverRange. */
static bool matches_driver_range(const uint32_t version[3], const char *expression)
{
    char token[64];
    const char *cursor = expression;
    while (*cursor) {
        const char *end = strchr(cursor, ',');
        size_t length = end ? (size_t)(end - cursor) : strlen(cursor);
        while (length && *cursor == ' ') { cursor++; length--; }
        while (length && cursor[length - 1] == ' ') length--;
        if (length >= sizeof(token)) return false;
        memcpy(token, cursor, length);
        token[length] = '\0';
        cursor = end ? end + 1 : cursor + strlen(cursor);
        if (!length) continue;
        const char *operand = token;
        int need = 0; /* 1 ge, 2 le, 3 gt, 4 lt, 5 eq */
        if (!strncmp(token, ">=", 2)) { need = 1; operand += 2; }
        else if (!strncmp(token, "<=", 2)) { need = 2; operand += 2; }
        else if (!strncmp(token, "==", 2)) { need = 5; operand += 2; }
        else if (token[0] == '>') { need = 3; operand += 1; }
        else if (token[0] == '<') { need = 4; operand += 1; }
        else need = 5;
        uint32_t rhs[3];
        if (!doe_semver_parse(operand, rhs)) return false;
        int order = semver_cmp(version, rhs);
        if ((need == 1 && order < 0) || (need == 2 && order > 0) || (need == 3 && order <= 0) ||
            (need == 4 && order >= 0) || (need == 5 && order != 0)) return false;
    }
    return true;
}

static bool supports(doe_quirk_scope scope, bucket_kind kind)
{
    switch (scope) {
    case DOE_SCOPE_ALIGNMENT: return kind == BUCKET_UPLOAD || kind == BUCKET_COPY;
    case DOE_SCOPE_LAYOUT: return kind != BUCKET_UPLOAD && kind != BUCKET_BARRIER;
    case DOE_SCOPE_BARRIER:
        return kind == BUCKET_BARRIER || kind == BUCKET_KERNEL_DISPATCH || kind == BUCKET_RENDER_DRAW || kind == BUCKET_DISPATCH;
    case DOE_SCOPE_DRIVER_TOGGLE: return true;
    case DOE_SCOPE_MEMORY:
        return kind == BUCKET_COPY || kind == BUCKET_UPLOAD || kind == BUCKET_TEXTURE_WRITE || kind == BUCKET_TEXTURE_QUERY ||
            kind == BUCKET_TEXTURE_DESTROY;
    }
    return false;
}

/* scoreRule in quirk/runtime.zig, for the bucket's command kind. */
static uint32_t score(const doe_quirk *q, bucket_kind kind, const doe_profile *profile)
{
    uint32_t s = q->priority;
    if (q->family) { if (profile->family && !strcmp(q->family, profile->family)) s += 50; }
    else s += 1;
    if (q->driver_range) s += 10;
    if (q->safety == 3) s += 15;
    if (q->safety == 2) s += 8;
    if (q->verification == 2) s += 12;
    if (q->scope == DOE_SCOPE_MEMORY && profile->family && q->family) s += 20;
    switch (kind) {
    case BUCKET_UPLOAD:
        if (q->scope == DOE_SCOPE_ALIGNMENT) s += 5;
        if (q->scope == DOE_SCOPE_MEMORY) s += 3;
        break;
    case BUCKET_COPY:
        if (q->scope == DOE_SCOPE_MEMORY) s += 8;
        if (q->scope == DOE_SCOPE_ALIGNMENT) s += 4;
        break;
    case BUCKET_BARRIER: if (q->scope == DOE_SCOPE_BARRIER) s += 8; break;
    case BUCKET_KERNEL_DISPATCH:
        if (q->scope == DOE_SCOPE_LAYOUT) s += 7;
        if (q->scope == DOE_SCOPE_BARRIER) s += 2;
        break;
    case BUCKET_RENDER_DRAW:
        if (q->scope == DOE_SCOPE_LAYOUT) s += 6;
        if (q->scope == DOE_SCOPE_BARRIER) s += 3;
        break;
    case BUCKET_SAMPLER_CREATE:
    case BUCKET_SAMPLER_DESTROY: if (q->scope == DOE_SCOPE_LAYOUT) s += 3; break;
    case BUCKET_TEXTURE_WRITE:
    case BUCKET_TEXTURE_QUERY:
    case BUCKET_TEXTURE_DESTROY:
        if (q->scope == DOE_SCOPE_MEMORY) s += 6;
        if (q->scope == DOE_SCOPE_LAYOUT) s += 2;
        break;
    case BUCKET_DISPATCH:
        if (q->scope == DOE_SCOPE_LAYOUT) s += 4;
        if (q->scope == DOE_SCOPE_BARRIER) s += 6;
        break;
    case BUCKET_COUNT: break;
    }
    return s;
}

static int proof_priority(int proof) { return proof == 0 ? 3 : proof == 1 ? 2 : 1; }

/* compareScoredQuirk: score, then proof level, then priority, then the smaller id. */
static bool better(const doe_quirk *a, uint32_t a_score, const doe_quirk *b, uint32_t b_score)
{
    if (a_score != b_score) return a_score > b_score;
    if (proof_priority(a->proof) != proof_priority(b->proof)) return proof_priority(a->proof) > proof_priority(b->proof);
    if (a->priority != b->priority) return a->priority > b->priority;
    return strcmp(a->id, b->id) < 0;
}

static bool matches_profile(const doe_quirk *q, const doe_profile *profile)
{
    if (strcasecmp(profile->vendor, q->vendor) || strcasecmp(profile->api, q->api)) return false;
    if (q->family && (!profile->family || strcmp(q->family, profile->family))) return false;
    return !q->driver_range || matches_driver_range(profile->driver, q->driver_range);
}

static bool fail(char *message, size_t size, size_t index, const char *format, const char *detail)
{
    char reason[160];
    snprintf(reason, sizeof(reason), format, detail);
    snprintf(message, size, "quirk %zu: %s", index, reason);
    return false;
}

static const char *string_field(const doe_json_value *object, const char *key, bool required, bool *ok)
{
    const doe_json_value *value = doe_json_member_value(object, key);
    if (!value || value->kind == DOE_JSON_NULL) { if (required) *ok = false; return NULL; }
    if (value->kind != DOE_JSON_STRING) { *ok = false; return NULL; }
    return value->string;
}

static bool only(const doe_json_value *object, const char *const *allowed)
{
    for (size_t i = 0; i < object->count; i++) {
        bool known = false;
        for (const char *const *name = allowed; *name; name++) known |= !strcmp(object->members[i].key, *name);
        if (!known) return false;
    }
    return true;
}

/* quirk_json.zig: an array ignores unknown members; a lone object rejects them. */
static bool parse_quirk(const doe_json_value *object, bool strict, size_t index, doe_quirk *q, char *message, size_t size)
{
    static const char *const top[] = {"schemaVersion", "quirkId", "scope", "match", "action", "safetyClass",
        "verificationMode", "proofLevel", "provenance", NULL};
    static const char *const match_fields[] = {"vendor", "api", "deviceFamily", "driverRange", NULL};
    static const char *const action_fields[] = {"kind", "params", NULL};
    static const char *const provenance_fields[] = {"sourceRepo", "sourcePath", "sourceCommit", "observedAt", NULL};
    if (object->kind != DOE_JSON_OBJECT) return fail(message, size, index, "%s", "is not an object");
    const doe_json_value *match = doe_json_member_value(object, "match");
    const doe_json_value *action = doe_json_member_value(object, "action");
    const doe_json_value *provenance = doe_json_member_value(object, "provenance");
    if (!match || match->kind != DOE_JSON_OBJECT || !action || action->kind != DOE_JSON_OBJECT || !provenance ||
        provenance->kind != DOE_JSON_OBJECT)
        return fail(message, size, index, "%s", "needs match, action and provenance objects");
    if (strict && (!only(object, top) || !only(match, match_fields) || !only(action, action_fields) ||
        !only(provenance, provenance_fields)))
        return fail(message, size, index, "%s", "has a field doe-zig-runtime does not read");
    uint64_t version = 0;
    const doe_json_value *schema = doe_json_member_value(object, "schemaVersion");
    if (!schema || !doe_json_as_u64(schema, UINT8_MAX, &version) || version != SCHEMA_VERSION)
        return fail(message, size, index, "%s", "schemaVersion must be 2");
    bool ok = true;
    q->id = string_field(object, "quirkId", true, &ok);
    const char *scope = string_field(object, "scope", true, &ok);
    const char *safety = string_field(object, "safetyClass", true, &ok);
    const char *verification = string_field(object, "verificationMode", true, &ok);
    const char *proof = string_field(object, "proofLevel", true, &ok);
    q->vendor = string_field(match, "vendor", true, &ok);
    q->api = string_field(match, "api", true, &ok);
    q->family = string_field(match, "deviceFamily", false, &ok);
    q->driver_range = string_field(match, "driverRange", false, &ok);
    const char *kind = string_field(action, "kind", true, &ok);
    for (const char *const *name = provenance_fields; *name; name++) string_field(provenance, *name, true, &ok);
    if (!ok) return fail(message, size, index, "%s", "a required string field is missing or not a string");
    int values[5] = {lookup(SCOPES, scope), lookup(SAFETY, safety), lookup(VERIFICATION, verification),
        lookup(PROOF, proof), lookup(APIS, q->api)};
    for (int i = 0; i < 5; i++) if (values[i] < 0) return fail(message, size, index, "%s", "has an unknown scope, class, mode, level or api");
    q->scope = (doe_quirk_scope)values[0];
    q->safety = values[1];
    q->verification = values[2];
    q->proof = values[3];
    int action_kind = -1;
    for (int i = 0; ACTIONS[i]; i++) if (!strcmp(ACTIONS[i], kind)) action_kind = i; /* case-sensitive, as Zig */
    if (action_kind < 0) return fail(message, size, index, "action kind \"%s\" is not supported", kind);
    q->action = (doe_quirk_action_kind)action_kind;
    const doe_json_value *params = doe_json_member_value(action, "params");
    if (params && params->kind == DOE_JSON_NULL) params = NULL;
    uint64_t number = 0;
    if (q->action == DOE_ACTION_NO_OP && params) return fail(message, size, index, "%s", "no_op takes no params");
    if (q->action == DOE_ACTION_USE_TEMPORARY_BUFFER) {
        const doe_json_value *field = params && params->kind == DOE_JSON_OBJECT ? doe_json_member_value(params, "bufferAlignmentBytes") : NULL;
        if (!field || !doe_json_as_u64(field, UINT32_MAX, &number) || !number)
            return fail(message, size, index, "%s", "use_temporary_buffer needs a nonzero bufferAlignmentBytes");
        q->alignment_bytes = (uint32_t)number;
    }
    if (q->action == DOE_ACTION_USE_TEMPORARY_RENDER_TEXTURE) {
        const doe_json_value *field = params && params->kind == DOE_JSON_OBJECT ? doe_json_member_value(params, "minMipLevel") : NULL;
        if (!field || !doe_json_as_u64(field, UINT32_MAX, &number))
            return fail(message, size, index, "%s", "use_temporary_render_texture needs minMipLevel");
        q->min_mip_level = (uint32_t)number;
    }
    if (q->action == DOE_ACTION_TOGGLE) {
        const doe_json_value *field = params && params->kind == DOE_JSON_OBJECT ? doe_json_member_value(params, "toggle") : NULL;
        if (!field || field->kind != DOE_JSON_STRING) return fail(message, size, index, "%s", "toggle needs a toggle name");
        q->toggle = field->string;
    }
    /* inferPriority in quirk_json.zig. */
    q->priority = (q->scope == DOE_SCOPE_MEMORY ? 30 : 0) + (q->scope == DOE_SCOPE_BARRIER ? 20 : 0) +
        (q->verification == 2 ? 10 : 0) + (q->safety == 3 ? 100 : 0);
    return true;
}

/* Active mode applies actions; refuse the ones this runtime has no path for. */
static bool applicable(const doe_quirk *q)
{
    if (q->action == DOE_ACTION_USE_TEMPORARY_RENDER_TEXTURE) return false;
    if (q->action == DOE_ACTION_TOGGLE && doe_toggle_effect_of(q->toggle) == DOE_TOGGLE_BEHAVIORAL) return is_staging_toggle(q->toggle);
    return true;
}

bool doe_quirks_load(const char *text, size_t size, doe_quirk_mode mode, const doe_profile *profile, doe_quirk_set *set,
    char *message, size_t message_size)
{
    *set = (doe_quirk_set){.mode = mode};
    doe_json_error error;
    if (doe_json_parse(text, size, &set->document, &error) != DOE_JSON_OK) {
        snprintf(message, message_size, "quirks JSON: %s at byte %zu", doe_json_status_name(error.status), error.offset);
        return false;
    }
    const doe_json_value *root = set->document.root;
    bool list = root->kind == DOE_JSON_ARRAY;
    size_t count = list ? root->count : 1;
    set->quirks = count ? calloc(count, sizeof(doe_quirk)) : NULL;
    if (count && !set->quirks) { snprintf(message, message_size, "out of memory"); doe_quirks_free(set); return false; }
    for (size_t i = 0; i < count; i++) {
        doe_quirk *q = &set->quirks[i];
        if (!parse_quirk(list ? root->items[i] : root, !list, i, q, message, message_size)) { doe_quirks_free(set); return false; }
        if (mode == DOE_QUIRK_ACTIVE && !applicable(q)) {
            fail(message, message_size, i, "action of \"%s\" cannot be applied by doe-c-runtime (active mode)", q->id);
            doe_quirks_free(set);
            return false;
        }
    }
    set->count = count;
    for (size_t i = 0; i < count; i++) {
        const doe_quirk *q = &set->quirks[i];
        if (!matches_profile(q, profile)) continue;
        for (int k = 0; k < BUCKET_COUNT; k++) {
            if (!supports(q->scope, (bucket_kind)k)) continue;
            doe_quirk_bucket *b = &set->buckets[k];
            uint32_t s = score(q, (bucket_kind)k, profile);
            b->matched_count++;
            if (!b->best || better(q, s, b->best, b->best_score)) { b->best = q; b->best_score = s; }
        }
    }
    /* finalizeBucket for a build without Lean verification (lean_verified false). */
    for (int k = 0; k < BUCKET_COUNT; k++) {
        doe_quirk_bucket *b = &set->buckets[k];
        if (!b->best) continue;
        b->requires_lean = b->best->verification == 2;
        b->blocking = b->requires_lean && b->best->proof != 0;
    }
    return true;
}

void doe_quirks_free(doe_quirk_set *set)
{
    free(set->quirks);
    doe_json_free(&set->document);
    *set = (doe_quirk_set){0};
}

static bucket_kind bucket_of(doe_command_kind kind)
{
    switch (kind) {
    case DOE_COMMAND_UPLOAD:
    case DOE_COMMAND_BUFFER_WRITE: return BUCKET_UPLOAD;
    case DOE_COMMAND_COPY: return BUCKET_COPY;
    case DOE_COMMAND_BARRIER: return BUCKET_BARRIER;
    case DOE_COMMAND_KERNEL_DISPATCH: return BUCKET_KERNEL_DISPATCH;
    case DOE_COMMAND_RENDER_DRAW:
    case DOE_COMMAND_RENDER_PASS: return BUCKET_RENDER_DRAW;
    case DOE_COMMAND_SAMPLER_CREATE: return BUCKET_SAMPLER_CREATE;
    case DOE_COMMAND_SAMPLER_DESTROY: return BUCKET_SAMPLER_DESTROY;
    case DOE_COMMAND_TEXTURE_WRITE: return BUCKET_TEXTURE_WRITE;
    case DOE_COMMAND_TEXTURE_QUERY: return BUCKET_TEXTURE_QUERY;
    case DOE_COMMAND_TEXTURE_DESTROY: return BUCKET_TEXTURE_DESTROY;
    case DOE_COMMAND_DISPATCH:
    case DOE_COMMAND_DISPATCH_INDIRECT: return BUCKET_DISPATCH;
    }
    return BUCKET_COUNT;
}

doe_quirk_decision doe_quirks_decide(const doe_quirk_set *set, doe_command_kind kind)
{
    doe_quirk_decision d = {.scope = "none", .safety = "none", .verification = "none", .proof = "none", .action = "none"};
    bucket_kind k = bucket_of(kind);
    if (!set || set->mode == DOE_QUIRK_OFF || k == BUCKET_COUNT) return d;
    const doe_quirk_bucket *b = &set->buckets[k];
    d.matched_count = b->matched_count;
    if (!b->best) return d;
    const doe_quirk *q = b->best;
    d.matched_id = q->id;
    d.toggle = q->action == DOE_ACTION_TOGGLE ? q->toggle : NULL;
    d.scope = SCOPES[q->scope];
    d.safety = SAFETY[q->safety];
    d.verification = VERIFICATION[q->verification];
    d.proof = PROOF[q->proof];
    d.action = ACTIONS[q->action];
    d.score = b->best_score;
    d.requires_lean = b->requires_lean;
    d.blocking = b->blocking;
    /* applyAction (quirk_actions.zig): only copies change, through a staging buffer. */
    if (set->mode == DOE_QUIRK_ACTIVE && kind == DOE_COMMAND_COPY) {
        if (q->action == DOE_ACTION_USE_TEMPORARY_BUFFER) {
            d.temporary_buffer = true;
            d.temporary_buffer_alignment = q->alignment_bytes;
        } else if (q->action == DOE_ACTION_TOGGLE && doe_toggle_effect_of(q->toggle) == DOE_TOGGLE_BEHAVIORAL &&
            is_staging_toggle(q->toggle)) {
            d.temporary_buffer = true;
            d.temporary_buffer_alignment = STAGING_TOGGLE_ALIGNMENT;
        }
    }
    return d;
}
