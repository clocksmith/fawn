/* SPDX-License-Identifier: MIT
 * Quirk parsing, profile matching and decisions against doe-zig-runtime's rules
 * (quirk_json.zig, runtime.zig): the repository's example quirk files, the
 * score of a matched quirk per command kind, ties, driver ranges, the trace
 * fields of a decision, the active-mode staging buffer, and refusal of invalid
 * quirks and of actions this runtime cannot apply. argv[1] is the examples
 * directory. */
#include "quirks.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static doe_profile intel(void)
{
    doe_profile p = {.vendor = "intel", .api = "vulkan", .family = "gen12"};
    assert(doe_semver_parse("31.0.101", p.driver));
    return p;
}

static bool load(const char *text, doe_quirk_mode mode, const doe_profile *profile, doe_quirk_set *set, char *message)
{
    return doe_quirks_load(text, strlen(text), mode, profile, set, message, 256);
}

static char *slurp(const char *root, const char *name)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/quirks/%s", root, name);
    FILE *file = fopen(path, "rb");
    assert(file);
    char *text = calloc(1, 1 << 16);
    assert(text && fread(text, 1, (1 << 16) - 1, file) > 0);
    fclose(file);
    return text;
}

static const char *QUIRK = "{\"schemaVersion\":2,\"quirkId\":\"%s\",\"scope\":\"%s\",\"match\":{\"vendor\":\"%s\",\"api\":\"vulkan\"%s},"
    "\"action\":%s,\"safetyClass\":\"%s\",\"verificationMode\":\"%s\",\"proofLevel\":\"%s\",\"provenance\":{\"sourceRepo\":\"t\","
    "\"sourcePath\":\"t\",\"sourceCommit\":\"t\",\"observedAt\":\"t\"}}";

static void quirk(char *out, size_t size, const char *id, const char *scope, const char *vendor, const char *match_extra,
    const char *action, const char *safety, const char *verification, const char *proof)
{
    snprintf(out, size, QUIRK, id, scope, vendor, match_extra, action, safety, verification, proof);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    doe_profile p = intel();
    char message[256];
    doe_quirk_set set;
    /* The empty lists the bench templates pass decide nothing. */
    char *text = slurp(argv[1], "amd_radv_noop_list.json");
    assert(load(text, DOE_QUIRK_TRACE, &p, &set, message) && set.count == 0);
    doe_quirk_decision none = doe_quirks_decide(&set, DOE_COMMAND_COPY);
    assert(!none.matched_id && !strcmp(none.scope, "none") && none.matched_count == 0);
    doe_quirks_free(&set);
    free(text);
    /* One object; driver_toggle scope matches every kind: 0 + family 50 + range 10 + high 8. */
    text = slurp(argv[1], "intel_gen12_temp_buffer.json");
    assert(load(text, DOE_QUIRK_TRACE, &p, &set, message));
    doe_quirk_decision d = doe_quirks_decide(&set, DOE_COMMAND_KERNEL_DISPATCH);
    assert(d.matched_id && !strcmp(d.matched_id, "vulkan.intel.gen12.use_temp_buffer_compressed_copy"));
    assert(d.score == 68 && d.matched_count == 1 && !strcmp(d.scope, "driver_toggle") && !strcmp(d.action, "use_temporary_buffer"));
    assert(!strcmp(d.verification, "lean_preferred") && !strcmp(d.proof, "guarded") && !d.requires_lean && !d.blocking && !d.toggle);
    assert(!doe_quirks_decide(&set, DOE_COMMAND_COPY).temporary_buffer); /* trace mode applies nothing */
    doe_quirks_free(&set);
    assert(load(text, DOE_QUIRK_ACTIVE, &p, &set, message));
    d = doe_quirks_decide(&set, DOE_COMMAND_COPY);
    assert(d.temporary_buffer && d.temporary_buffer_alignment == 4);
    assert(!doe_quirks_decide(&set, DOE_COMMAND_UPLOAD).temporary_buffer); /* only copies change */
    doe_quirks_free(&set);
    /* Out of the driver range or for another vendor: no match. */
    doe_profile older = p;
    assert(doe_semver_parse("30.9", older.driver));
    assert(load(text, DOE_QUIRK_TRACE, &older, &set, message) && !doe_quirks_decide(&set, DOE_COMMAND_COPY).matched_id);
    doe_quirks_free(&set);
    doe_profile amd = p;
    amd.vendor = "AMD";
    assert(load(text, DOE_QUIRK_TRACE, &amd, &set, message) && !doe_quirks_decide(&set, DOE_COMMAND_COPY).matched_id);
    doe_quirks_free(&set);
    free(text);
    /* The sample quirk doe-zig-runtime embeds scores 126 on a copy (memory scope). */
    char a[1024], b[1024], list[2400];
    quirk(a, sizeof(a), "vulkan.intel.gen12.use_temp_buffer_compressed_copy", "memory", "intel",
        ",\"deviceFamily\":\"gen12\",\"driverRange\":\">=31.0.101,<32.0.0\"",
        "{\"kind\":\"use_temporary_buffer\",\"params\":{\"bufferAlignmentBytes\":4}}", "high", "lean_preferred", "guarded");
    snprintf(list, sizeof(list), "[%s]", a);
    assert(load(list, DOE_QUIRK_TRACE, &p, &set, message));
    assert(doe_quirks_decide(&set, DOE_COMMAND_COPY).score == 126 && doe_quirks_decide(&set, DOE_COMMAND_UPLOAD).score == 121);
    assert(!doe_quirks_decide(&set, DOE_COMMAND_KERNEL_DISPATCH).matched_id); /* memory scope skips dispatches */
    doe_quirks_free(&set);
    /* Ties: same score and proof level, then priority, then the smaller id wins; both count. */
    quirk(a, sizeof(a), "b.toggle", "driver_toggle", "intel", "", "{\"kind\":\"toggle\",\"params\":{\"toggle\":\"use_dxc\"}}",
        "low", "guard_only", "guarded");
    quirk(b, sizeof(b), "a.noop", "driver_toggle", "intel", "", "{\"kind\":\"no_op\"}", "low", "guard_only", "guarded");
    snprintf(list, sizeof(list), "[%s,%s]", a, b);
    assert(load(list, DOE_QUIRK_ACTIVE, &p, &set, message));
    d = doe_quirks_decide(&set, DOE_COMMAND_BARRIER);
    assert(!strcmp(d.matched_id, "a.noop") && d.matched_count == 2 && d.score == 1);
    doe_quirks_free(&set);
    /* lean_required without a proven level blocks (no Lean-verified build). */
    quirk(a, sizeof(a), "lean", "barrier", "intel", "", "{\"kind\":\"no_op\"}", "critical", "lean_required", "guarded");
    snprintf(list, sizeof(list), "[%s]", a);
    assert(load(list, DOE_QUIRK_TRACE, &p, &set, message));
    d = doe_quirks_decide(&set, DOE_COMMAND_BARRIER);
    assert(d.requires_lean && d.blocking && d.score == 20 + 10 + 100 + 1 + 15 + 12 + 8);
    doe_quirks_free(&set);
    /* A behavioral staging toggle stages copies at 256 bytes in active mode. */
    quirk(a, sizeof(a), "stage", "layout", "intel", "",
        "{\"kind\":\"toggle\",\"params\":{\"toggle\":\"use_temporary_buffer_in_texture_to_texture_copy\"}}", "low", "guard_only", "proven");
    snprintf(list, sizeof(list), "[%s]", a);
    assert(load(list, DOE_QUIRK_ACTIVE, &p, &set, message));
    d = doe_quirks_decide(&set, DOE_COMMAND_COPY);
    assert(d.temporary_buffer && d.temporary_buffer_alignment == 256 && !strcmp(d.toggle, "use_temporary_buffer_in_texture_to_texture_copy"));
    doe_quirks_free(&set);
    /* Refusals: an action this runtime cannot apply in active mode (trace mode records it). */
    quirk(a, sizeof(a), "render", "layout", "intel", "", "{\"kind\":\"use_temporary_render_texture\",\"params\":{\"minMipLevel\":2}}",
        "high", "guard_only", "guarded");
    snprintf(list, sizeof(list), "[%s]", a);
    assert(!load(list, DOE_QUIRK_ACTIVE, &p, &set, message) && strstr(message, "cannot be applied"));
    assert(load(list, DOE_QUIRK_TRACE, &p, &set, message));
    doe_quirks_free(&set);
    quirk(a, sizeof(a), "metal", "layout", "intel", "",
        "{\"kind\":\"toggle\",\"params\":{\"toggle\":\"MetalRenderR8RG8UnormSmallMipToTempTexture\"}}", "high", "guard_only", "guarded");
    snprintf(list, sizeof(list), "[%s]", a);
    assert(!load(list, DOE_QUIRK_ACTIVE, &p, &set, message) && strstr(message, "cannot be applied"));
    /* Invalid quirks: unknown action, wrong schema, unknown enum value, a lone object's extra field. */
    quirk(a, sizeof(a), "x", "layout", "intel", "", "{\"kind\":\"rewrite\"}", "low", "guard_only", "guarded");
    snprintf(list, sizeof(list), "[%s]", a);
    assert(!load(list, DOE_QUIRK_TRACE, &p, &set, message) && strstr(message, "not supported"));
    quirk(a, sizeof(a), "x", "layout", "intel", "", "{\"kind\":\"no_op\"}", "low", "guard_only", "maybe");
    assert(!load(a, DOE_QUIRK_TRACE, &p, &set, message) && strstr(message, "unknown"));
    assert(!load("[{\"schemaVersion\":1}]", DOE_QUIRK_TRACE, &p, &set, message));
    quirk(a, sizeof(a), "x", "layout", "intel", "", "{\"kind\":\"no_op\"}", "low", "guard_only", "guarded");
    a[strlen(a) - 1] = '\0';
    strcat(a, ",\"notes\":\"x\"}");
    assert(!load(a, DOE_QUIRK_TRACE, &p, &set, message) && strstr(message, "does not read"));
    snprintf(list, sizeof(list), "[%s]", a);
    assert(load(list, DOE_QUIRK_TRACE, &p, &set, message)); /* arrays ignore extra fields, as Zig */
    doe_quirks_free(&set);
    assert(!doe_semver_parse("31.a", p.driver) && !doe_semver_parse("1.2.3.4", p.driver) && !doe_semver_parse("", p.driver));
    puts("runtime-cli quirks: example files, scores, ties, driver ranges, blocking, active staging and 7 refusals (host only)");
    return 0;
}
