/* SPDX-License-Identifier: MIT
 * Quirk matching for runtime command streams, following doe-zig-runtime's
 * quirk owner (runtime/zig/src/quirk): schema 2 quirk JSON, a device profile
 * (vendor, api, family, driver), per-command-kind best-quirk selection by the
 * same score and tie rules, and the trace decision fields the row hash covers.
 * Mode "trace" records decisions; "active" also applies actions, and a quirk
 * set with an action this runtime cannot apply is refused. */
#ifndef DOE_RUNTIME_QUIRKS_H
#define DOE_RUNTIME_QUIRKS_H
#include "stream.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum doe_toggle_effect { DOE_TOGGLE_BEHAVIORAL, DOE_TOGGLE_INFORMATIONAL, DOE_TOGGLE_UNHANDLED } doe_toggle_effect;
typedef struct doe_toggle_entry {
    const char *name;
    doe_toggle_effect effect;
} doe_toggle_entry;
/* Generated from config/quirk-toggle-registry.json at build time. */
extern const doe_toggle_entry DOE_TOGGLE_REGISTRY[];
extern const size_t DOE_TOGGLE_REGISTRY_COUNT;
doe_toggle_effect doe_toggle_effect_of(const char *name);

typedef enum doe_quirk_mode { DOE_QUIRK_OFF, DOE_QUIRK_TRACE, DOE_QUIRK_ACTIVE } doe_quirk_mode;
typedef enum doe_quirk_scope { DOE_SCOPE_ALIGNMENT, DOE_SCOPE_BARRIER, DOE_SCOPE_LAYOUT, DOE_SCOPE_DRIVER_TOGGLE, DOE_SCOPE_MEMORY } doe_quirk_scope;
typedef enum doe_quirk_action_kind {
    DOE_ACTION_USE_TEMPORARY_BUFFER,
    DOE_ACTION_USE_TEMPORARY_RENDER_TEXTURE,
    DOE_ACTION_TOGGLE,
    DOE_ACTION_NO_OP
} doe_quirk_action_kind;

typedef struct doe_quirk {
    const char *id; /* Document-owned strings. */
    doe_quirk_scope scope;
    int safety;       /* 0 low, 1 moderate, 2 high, 3 critical */
    int verification; /* 0 guard_only, 1 lean_preferred, 2 lean_required */
    int proof;        /* 0 proven, 1 guarded, 2 rejected */
    const char *vendor, *api, *family, *driver_range;
    doe_quirk_action_kind action;
    uint32_t alignment_bytes, min_mip_level;
    const char *toggle;
    uint32_t priority;
} doe_quirk;

typedef struct doe_profile {
    const char *vendor, *api, *family;
    uint32_t driver[3];
} doe_profile;

/* The trace fields of one command's decision; "none" strings when no quirk matched. */
typedef struct doe_quirk_decision {
    const char *matched_id, *toggle;
    const char *scope, *safety, *verification, *proof, *action;
    uint64_t score, matched_count;
    bool requires_lean, blocking;
    /* Active mode only: the command copies through a temporary buffer of this alignment. */
    bool temporary_buffer;
    uint32_t temporary_buffer_alignment;
} doe_quirk_decision;

typedef struct doe_quirk_bucket {
    const doe_quirk *best;
    uint32_t best_score, matched_count;
    bool requires_lean, blocking, identity;
} doe_quirk_bucket;

enum { DOE_QUIRK_BUCKETS = 12 };
typedef struct doe_quirk_set {
    doe_json_document document;
    doe_quirk *quirks;
    size_t count;
    doe_quirk_mode mode;
    doe_quirk_bucket buckets[DOE_QUIRK_BUCKETS];
} doe_quirk_set;

/* "major[.minor[.patch]]" with decimal parts, as doe-zig-runtime's SemVer. */
bool doe_semver_parse(const char *text, uint32_t out[3]);
/* Parses a quirk file (an array, "[]", or one quirk object) and builds the
 * profile's buckets. False with a message for any invalid quirk, and in active
 * mode for any quirk whose action this runtime cannot apply. */
bool doe_quirks_load(const char *text, size_t size, doe_quirk_mode mode, const doe_profile *profile, doe_quirk_set *set,
    char *message, size_t message_size);
void doe_quirks_free(doe_quirk_set *set);
/* The decision for a command of this kind (an empty set decides nothing). */
doe_quirk_decision doe_quirks_decide(const doe_quirk_set *set, doe_command_kind kind);
#endif
