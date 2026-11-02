/* SPDX-License-Identifier: MIT */
#include "render_internal.h"
#include "../../../common/include/doe_draw_guard.h"

enum { RECORD_WORDS = 10, PARAMETER_WORDS = 9 };
typedef struct guard_plan {
    uint64_t limits[2], count_limit, first, end;
    size_t record, windows, cohort;
    bool cohort_start;
} guard_plan;

/* Compiler-owned buffer-size tables carry a u32 byte span even where the
 * native allocation and its per-cohort offset can exceed four GiB. */
uint32_t doe_mtl_draw_guard_cohort_limit(uint32_t maximum) {
    const uint32_t span_limit = UINT32_MAX / DOE_DRAW_GUARD_RECORD_V2_BYTES;
    return maximum < span_limit ? maximum : span_limit;
}

static uint32_t window_count(const doe_mtl_render_draw *draw, uint64_t first, uint64_t end) {
    uint64_t element = draw->index_type == 0 ? 2u : 4u;
    uint64_t offset = draw->index_offset + first * element;
    uint64_t maximum = (UINT64_C(0xfffffffc) - offset % 4) / element;
    uint64_t remaining = end - first;
    return (uint32_t)(remaining < maximum ? remaining : maximum);
}
static doe_mtl_status plan_draw(const doe_mtl_render_draw *draw, guard_plan *plan) {
    doe_mtl_status status = doe_mtl_draw_guard_capacities(draw, plan->limits);
    if (status != DOE_MTL_OK) return status;
    const doe_mtl_guarded_draw *input = draw->guarded;
    plan->count_limit = draw->index_buffer ? input->index_size / (draw->index_type == 0 ? 2u : 4u) : plan->limits[0];
    plan->end = plan->count_limit;
    if (plan->end > UINT64_C(2) * UINT32_MAX) plan->end = UINT64_C(2) * UINT32_MAX;
    if (draw->index_buffer && !input->arguments) {
        plan->first = input->first_index;
        uint64_t end = plan->first + draw->count;
        if (end < plan->end) plan->end = end;
    }
    if (draw->index_buffer && plan->limits[0] != UINT64_MAX)
        for (uint64_t first = plan->first; first < plan->end;) {
            uint32_t count = window_count(draw, first, plan->end);
            if (!count || plan->windows == SIZE_MAX) return DOE_MTL_RANGE;
            ++plan->windows;
            first += count;
        }
    return DOE_MTL_OK;
}
static doe_mtl_native_guard_dispatch dispatch_step(doe_mtl_pipeline *pipeline, doe_mtl_dimensions groups) {
    return (doe_mtl_native_guard_dispatch){.pipeline=pipeline->native,.sizes_slot=pipeline->sizes_slot,
        .groups=groups,.workgroup=pipeline->workgroup};
}
static void bind_step(doe_mtl_native_guard_dispatch *step, uint32_t slot, doe_mtl_buffer *buffer,
    size_t offset, uint32_t size) {
    uint32_t at = step->binding_count++;
    step->slots[at] = slot;
    step->bindings[at] = (doe_mtl_native_binding){buffer->native,offset,size};
}

/* One call-borrowed plan per complete pass. The ordinary command owner retains
 * both arenas, argument/index buffers and PSOs before the native batch starts.
 * No CPU read of caller arguments or indices, and no scratch reuse across passes. */
doe_mtl_status doe_mtl_draw_guard_batch(doe_mtl_commands *commands, const doe_mtl_render_draw *draws,
    doe_mtl_native_render_draw *native_draws, size_t count, doe_mtl_error *error) {
    size_t unique = 0, copies_count = 0, steps_count = 0, windows = 0, cohorts = 0;
    for (size_t i = 0; i < count; ++i) if (draws[i].guarded) ++unique;
    if (!unique) return doe_mtl_fail(DOE_MTL_OK, "no draw guards", error);
    doe_mtl_native *native = &commands->context->native;
    if (!native->guard_batch) return doe_mtl_fail(DOE_MTL_UNSUPPORTED, "draw guard batch boundary", error);
    if (count > SIZE_MAX / sizeof(guard_plan)) return doe_mtl_fail(DOE_MTL_RANGE, "draw guard plan count", error);
    guard_plan *plans = calloc(count, sizeof(*plans));
    if (!plans) return doe_mtl_fail(DOE_MTL_OUT_OF_MEMORY, "draw guard plans", error);
    doe_mtl_status status = DOE_MTL_OK;
    unique = 0;
    const doe_mtl_draw_guard *previous_guard = NULL;
    uint32_t cohort_count = 0;
    for (size_t i = 0; i < count && status == DOE_MTL_OK; ++i) {
        if (!draws[i].guarded) continue;
        if (i && doe_mtl_draw_guard_same_inputs(&draws[i-1], &draws[i])) {
            plans[i] = plans[i-1];
            continue;
        }
        status = plan_draw(&draws[i], &plans[i]);
        plans[i].record = unique++;
        const doe_mtl_draw_guard *guard = draws[i].guarded->guard;
        if (guard != previous_guard || cohort_count == doe_mtl_draw_guard_cohort_limit(guard->max_groups_x)) {
            plans[i].cohort_start = true;
            previous_guard = guard;
            cohort_count = 0;
            ++cohorts;
        }
        plans[i].cohort = cohorts - 1;
        ++cohort_count;
        if (plans[i].windows > SIZE_MAX - windows) status = DOE_MTL_RANGE;
        else windows += plans[i].windows;
        if (draws[i].guarded->arguments) ++copies_count;
    }
    if (cohorts > SIZE_MAX - windows) status = DOE_MTL_RANGE;
    else steps_count = cohorts + windows;
    if (unique > SIZE_MAX / DOE_DRAW_GUARD_RECORD_V2_BYTES || windows > SIZE_MAX / (PARAMETER_WORDS * sizeof(uint32_t)) ||
        copies_count > SIZE_MAX / sizeof(doe_mtl_native_guard_copy) ||
        steps_count > SIZE_MAX / sizeof(doe_mtl_native_guard_dispatch)) status = DOE_MTL_RANGE;
    size_t record_bytes = status == DOE_MTL_OK ? unique * DOE_DRAW_GUARD_RECORD_V2_BYTES : 0;
    size_t parameter_bytes = status == DOE_MTL_OK ? windows * PARAMETER_WORDS * sizeof(uint32_t) : 0;
    if (record_bytes > commands->context->caps.max_buffer_length ||
        parameter_bytes > commands->context->caps.max_buffer_length) status = DOE_MTL_RANGE;
    doe_mtl_native_guard_copy *copies = NULL;
    doe_mtl_native_guard_dispatch *steps = NULL;
    uint32_t *records = NULL, *parameters = NULL;
    doe_mtl_buffer *scratch = NULL, *params = NULL;
    if (status == DOE_MTL_OK) {
        copies = copies_count ? calloc(copies_count, sizeof(*copies)) : NULL;
        steps = calloc(steps_count, sizeof(*steps));
        records = calloc(unique, RECORD_WORDS * sizeof(uint32_t));
        parameters = windows ? calloc(windows, PARAMETER_WORDS * sizeof(uint32_t)) : NULL;
        if ((copies_count && !copies) || !steps || !records || (windows && !parameters)) status = DOE_MTL_OUT_OF_MEMORY;
    }
    if (status == DOE_MTL_OK) status = doe_mtl_buffer_create(commands->context, record_bytes, DOE_MTL_SHARED, &scratch, error);
    if (status == DOE_MTL_OK && parameter_bytes)
        status = doe_mtl_buffer_create(commands->context, parameter_bytes, DOE_MTL_SHARED, &params, error);
    size_t copy_at = 0, step_at = cohorts, parameter_at = 0;
    for (size_t i = 0; i < count && status == DOE_MTL_OK; ++i) {
        if (!draws[i].guarded) continue;
        const doe_mtl_render_draw *draw = &draws[i];
        const doe_mtl_guarded_draw *input = draw->guarded;
        const doe_mtl_draw_guard *guard = input->guard;
        const guard_plan *plan = &plans[i];
        size_t record_offset = plan->record * DOE_DRAW_GUARD_RECORD_V2_BYTES;
        native_draws[i].indirect_arguments = scratch->native;
        native_draws[i].indirect_offset = record_offset;
        if (i && doe_mtl_draw_guard_same_inputs(&draws[i-1], draw)) continue;
        uint32_t *words = records + plan->record * RECORD_WORDS;
        words[0]=draw->count; words[1]=draw->instance_count;
        words[2]=draw->index_buffer ? input->first_index : draw->first_vertex;
        words[3]=draw->index_buffer ? (uint32_t)draw->base_vertex : draw->first_instance;
        words[7]=(draw->index_buffer ? 1u : 0u) | (input->first_instance_allowed ? 2u : 0u);
        words[8]=(uint32_t)(plan->count_limit >> 32); words[9]=(uint32_t)(plan->limits[1] >> 32);
        if (draw->index_buffer) { words[4]=draw->first_instance; words[5]=(uint32_t)plan->count_limit; words[6]=(uint32_t)plan->limits[1]; }
        else { words[4]=(uint32_t)plan->count_limit; words[5]=(uint32_t)plan->limits[1]; }
        if (input->arguments) copies[copy_at++] = (doe_mtl_native_guard_copy){input->arguments->native,scratch->native,
            (size_t)input->argument_offset,record_offset,draw->index_buffer ? 20u : 16u};
        doe_mtl_native_guard_dispatch *normal = &steps[plan->cohort];
        if (plan->cohort_start) {
            *normal = dispatch_step(guard->normalize, (doe_mtl_dimensions){0,1,1});
            bind_step(normal, guard->record_slot, scratch, record_offset, 0);
        }
        ++normal->groups.x;
        normal->bindings[0].size += DOE_DRAW_GUARD_RECORD_V2_BYTES;
        for (uint64_t first = plan->first; plan->windows && first < plan->end;) {
            uint32_t n = window_count(draw, first, plan->end);
            uint64_t element = draw->index_type == 0 ? 2u : 4u;
            uint64_t offset = draw->index_offset + first * element, aligned = offset & ~UINT64_C(3);
            uint32_t bytes = (uint32_t)(((offset-aligned) + (uint64_t)n * element + 3) & ~UINT64_C(3));
            uint32_t groups = n / 64 + (n % 64 != 0);
            if (groups > guard->max_groups_x) groups = guard->max_groups_x;
            uint32_t *values = parameters + parameter_at * PARAMETER_WORDS;
            values[0]=(uint32_t)first; values[1]=(uint32_t)(first >> 32); values[2]=n;
            values[3]=(uint32_t)((offset-aligned)/2); values[4]=draw->index_type == 0;
            values[5]=(uint32_t)plan->limits[0]; values[6]=(uint32_t)(plan->limits[0] >> 32);
            values[7]=draw->topology == 2 || draw->topology == 4; values[8]=groups * 64;
            steps[step_at] = dispatch_step(guard->scan, (doe_mtl_dimensions){groups,1,1});
            bind_step(&steps[step_at], guard->scan_slots[0], scratch, record_offset, DOE_DRAW_GUARD_RECORD_V2_BYTES);
            bind_step(&steps[step_at], guard->scan_slots[1], draw->index_buffer, (size_t)aligned, bytes);
            bind_step(&steps[step_at++], guard->scan_slots[2], params, parameter_at++ * PARAMETER_WORDS * sizeof(uint32_t), PARAMETER_WORDS * sizeof(uint32_t));
            first += n;
        }
        status = doe_mtl_use_compute_pipeline(commands, guard->normalize);
        if (status == DOE_MTL_OK && plan->windows) status = doe_mtl_use_compute_pipeline(commands, guard->scan);
        doe_mtl_buffer_use *use;
        if (status == DOE_MTL_OK && input->arguments) status = doe_mtl_use_buffer(commands, input->arguments, &use);
    }
    if (status == DOE_MTL_OK) status = doe_mtl_buffer_write(scratch, 0, records, record_bytes);
    if (status == DOE_MTL_OK && parameter_bytes) status = doe_mtl_buffer_write(params, 0, parameters, parameter_bytes);
    doe_mtl_buffer_use *use;
    if (status == DOE_MTL_OK) status = doe_mtl_use_buffer(commands, scratch, &use);
    if (status == DOE_MTL_OK && params) status = doe_mtl_use_buffer(commands, params, &use);
    if (status == DOE_MTL_OK) status = native->guard_batch(native->userdata, commands->native,
        copies, copies_count, steps, steps_count, error);
    else if (!error || error->status == DOE_MTL_OK) (void)doe_mtl_fail(status, "draw guard batch preparation", error);
    doe_mtl_buffer_release(params); doe_mtl_buffer_release(scratch);
    free(parameters); free(records); free(steps); free(copies); free(plans);
    return status;
}
