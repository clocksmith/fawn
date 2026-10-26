/* SPDX-License-Identifier: MIT */
#include "numeric_eval.h"
#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static uint32_t argmax_token(const doe_ns_candidate *candidates, size_t count, int lane)
{
    size_t best = 0;
    for (size_t i = 1; i < count; i++) {
        double value = lane == 0 ? candidates[i].fast : lane == 1 ? candidates[i].stable : candidates[i].reference;
        double best_value = lane == 0 ? candidates[best].fast : lane == 1 ? candidates[best].stable : candidates[best].reference;
        if (value > best_value) best = i;
    }
    return candidates[best].token_id;
}

static bool finite_candidates(const doe_ns_candidate *values, size_t count, char *message, size_t message_size)
{
    for (size_t i = 0; i < count; i++)
        if (!isfinite(values[i].fast) || !isfinite(values[i].stable) || !isfinite(values[i].reference)) {
            snprintf(message, message_size, "numeric stability: nonfinite evaluation");
            return false;
        }
    return true;
}

bool doe_ns_binding_of(const doe_command *command, uint32_t index, const doe_kernel_binding **out, char *message,
    size_t message_size)
{
    for (size_t i = 0; i < command->as.dispatch.binding_count; i++) {
        const doe_kernel_binding *b = &command->as.dispatch.bindings[i];
        if (b->binding == index && b->group == 0) {
            if (b->resource != DOE_RESOURCE_BUFFER) {
                snprintf(message, message_size, "numeric stability: group 0 binding %u is not a buffer", index);
                return false;
            }
            *out = b;
            return true;
        }
    }
    snprintf(message, message_size, "numeric stability: %s has no group 0 binding %u", command->as.dispatch.kernel, index);
    return false;
}

bool doe_ns_capture_f32(const doe_ns_io *io, uint64_t handle, uint64_t offset, size_t count, double *out, char *message,
    size_t message_size)
{
    if (count > SIZE_MAX / 4 || count > (UINT64_MAX - offset) / 4) {
        snprintf(message, message_size, "numeric stability: capture range overflows");
        return false;
    }
    uint8_t *bytes = NULL;
    size_t length = 0;
    if (!io->capture(io->context, handle, offset, (uint64_t)count * 4, &bytes, &length, message, message_size)) return false;
    bool ok = length == count * 4;
    for (size_t i = 0; ok && i < count; i++) {
        uint32_t bits = (uint32_t)bytes[4 * i] | (uint32_t)bytes[4 * i + 1] << 8 | (uint32_t)bytes[4 * i + 2] << 16 |
            (uint32_t)bytes[4 * i + 3] << 24;
        float value;
        memcpy(&value, &bits, sizeof(value));
        out[i] = (double)value;
        if (!isfinite(value)) {
            free(bytes);
            snprintf(message, message_size, "numeric stability: nonfinite capture");
            return false;
        }
    }
    free(bytes);
    if (!ok) snprintf(message, message_size, "numeric stability: buffer %llu capture size mismatch", (unsigned long long)handle);
    return ok;
}

bool doe_ns_capture_uniform(const doe_ns_io *io, const doe_kernel_binding *b, uint8_t **bytes, size_t *length, char *message,
    size_t message_size)
{
    return io->capture(io->context, b->handle, b->buffer_offset, b->buffer_size, bytes, length, message, message_size);
}

uint32_t doe_ns_read_u32(const uint8_t *bytes, size_t length, size_t word)
{
    if (word >= length / 4) return 0;
    const uint8_t *b = bytes + word * 4;
    return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}

float doe_ns_read_f32(const uint8_t *bytes, size_t length, size_t word)
{
    uint32_t bits = doe_ns_read_u32(bytes, length, word);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

/* runtime_plan.zig buildAutoDecodeFinalLogitsObservation and
 * runtime_eval.zig evaluateMatmulObservation: uniform (rows, cols), weights
 * binding 1, hidden binding 2, logits binding 3. Ordinary matmul swaps
 * weights/hidden bindings while retaining the same evaluation order. */
bool doe_ns_evaluate_matmul(const doe_ns_io *io, doe_ns_evaluation *e, bool decode, char *message, size_t message_size)
{
    const doe_command *command = e->command;
    const doe_numeric_annotation *annotation = command->numeric;
    doe_numeric_capture hidden_capture, logits_capture, weights_capture;
    uint32_t rows, cols;
    if (annotation) {
        hidden_capture = annotation->hidden;
        logits_capture = annotation->logits;
        weights_capture = annotation->weights;
        if (annotation->candidate_count < 2 || annotation->candidate_count != logits_capture.count ||
            !hidden_capture.count || weights_capture.count < hidden_capture.count ||
            !command->semantic.op_id || strcmp(command->semantic.op_id, "matmul.logits")) {
            snprintf(message, message_size, "numeric stability: invalid annotated matmul shape or semantic operation");
            return false;
        }
        rows = logits_capture.count;
        cols = hidden_capture.count;
    } else {
        const doe_kernel_binding *uniform, *weights, *hidden, *logits;
        if (!doe_ns_binding_of(command, 0, &uniform, message, message_size) ||
            !doe_ns_binding_of(command, decode ? 1 : 2, &weights, message, message_size) ||
            !doe_ns_binding_of(command, decode ? 2 : 1, &hidden, message, message_size) ||
            !doe_ns_binding_of(command, 3, &logits, message, message_size)) return false;
        uint8_t *bytes = NULL;
        size_t length = 0;
        if (!doe_ns_capture_uniform(io, uniform, &bytes, &length, message, message_size)) return false;
        rows = doe_ns_read_u32(bytes, length, 0);
        cols = doe_ns_read_u32(bytes, length, 1);
        free(bytes);
        if (hidden->buffer_size == UINT64_MAX || logits->buffer_size == UINT64_MAX ||
            weights->buffer_size == UINT64_MAX || (uint64_t)cols * 4 > hidden->buffer_size ||
            (uint64_t)rows * 4 > logits->buffer_size ||
            (rows && (uint64_t)cols > weights->buffer_size / 4 / rows)) {
            snprintf(message, message_size, "numeric stability: matmul capture exceeds binding range");
            return false;
        }
        hidden_capture = (doe_numeric_capture){hidden->handle, hidden->buffer_offset, cols};
        logits_capture = (doe_numeric_capture){logits->handle, logits->buffer_offset, rows};
        weights_capture = (doe_numeric_capture){weights->handle, weights->buffer_offset, cols};
    }
    if (!rows || !cols || rows > (1u << 24) || cols > (1u << 24)) {
        snprintf(message, message_size, "numeric stability: logits shape %" PRIu32 "x%" PRIu32 " is not admitted", rows, cols);
        return false;
    }
    double *hidden_values = calloc(cols, sizeof(double)), *fast = calloc(rows, sizeof(double));
    double *stable = calloc(rows, sizeof(double)), *row = calloc(cols, sizeof(double));
    e->candidates = calloc(rows, sizeof(doe_ns_candidate));
    e->candidate_count = rows;
    bool ok = hidden_values && fast && stable && row && e->candidates &&
        doe_ns_capture_f32(io, hidden_capture.handle, hidden_capture.offset, cols, hidden_values, message, message_size) &&
        doe_ns_capture_f32(io, logits_capture.handle, logits_capture.offset, rows, fast, message, message_size);
    if (!ok && !message[0]) snprintf(message, message_size, "out of memory");
    for (uint32_t r = 0; ok && r < rows; r++) {
        uint32_t row_index = annotation ? annotation->candidates[r].row_index : r;
        uint64_t stride = (uint64_t)weights_capture.count * 4;
        if (stride && row_index > (UINT64_MAX - weights_capture.offset) / stride) {
            snprintf(message, message_size, "numeric stability: weights row offset overflows");
            ok = false;
            break;
        }
        uint64_t offset = weights_capture.offset + (uint64_t)row_index * stride;
        ok = doe_ns_capture_f32(io, weights_capture.handle, offset, cols, row, message, message_size);
        float f32_acc = 0;
        double f64_acc = 0;
        for (uint32_t c = 0; ok && c < cols; c++) {
            f32_acc = f32_acc + (float)hidden_values[c] * (float)row[c];
            f64_acc += hidden_values[c] * row[c];
        }
        /* Retained evaluation widens the serial f32 reduction before adding
         * the double annotation. Rounding bias to f32 changes selected tokens. */
        stable[r] = (double)f32_acc;
        if (annotation && annotation->candidates[r].has_bias) {
            stable[r] += annotation->candidates[r].bias;
            f64_acc += annotation->candidates[r].bias;
        }
        e->candidates[r] = (doe_ns_candidate){.token_id = annotation ? annotation->candidates[r].token_id : r,
            .label = annotation ? annotation->candidates[r].label : NULL,
            .fast = fast[r], .stable = stable[r], .reference = f64_acc};
    }
    ok = ok && finite_candidates(e->candidates, rows, message, message_size) && doe_ns_digest(fast, rows, e->fast_digest) && doe_ns_digest(stable, rows, e->stable_digest) &&
        doe_ns_layout_fingerprint(command, e->layout_fingerprint);
    if (ok) {
        e->fast_token = argmax_token(e->candidates, rows, 0);
        e->stable_token = argmax_token(e->candidates, rows, 1);
        e->reference_token = argmax_token(e->candidates, rows, 2);
        e->logits_handle = logits_capture.handle;
        e->logits_offset = logits_capture.offset;
    }
    free(hidden_values);
    free(fast);
    free(stable);
    free(row);
    return ok;
}

/* Preserve the retained f32_ops eight-lane accumulation order, including the
 * serial tail. This is numeric-policy evaluation, never shader execution. */
static double reduce_values(const double *left, const double *right, size_t count, bool stable)
{
    if (!stable) {
        double sum = 0;
        for (size_t i = 0; i < count; i++) sum += right ? left[i] * right[i] : left[i];
        return sum;
    }
    float lanes[8] = {0}, sum = 0, tail = 0;
    size_t i = 0;
    for (; count - i >= 8; i += 8)
        for (size_t lane = 0; lane < 8; lane++) {
            float value = (float)left[i + lane];
            if (right) value = value * (float)right[i + lane];
            lanes[lane] = lanes[lane] + value;
        }
    for (size_t lane = 0; lane < 8; lane++) sum = sum + lanes[lane];
    for (; i < count; i++) {
        float value = (float)left[i];
        if (right) value = value * (float)right[i];
        tail = tail + value;
    }
    return (double)(sum + tail);
}

static bool binding_count(const doe_kernel_binding *binding, size_t *count)
{
    if (binding->resource != DOE_RESOURCE_BUFFER || binding->buffer_size == UINT64_MAX ||
        binding->buffer_size % 4 || binding->buffer_size / 4 > SIZE_MAX / sizeof(double)) return false;
    *count = (size_t)(binding->buffer_size / 4);
    return *count > 0;
}

/* One-dimensional query, sequence-major keys and values; the retained policy
 * clamps shifted scores to [-30, 30] for both reference and stable lanes. */
static bool attention_values(const double *q, const double *k, const double *v, size_t sequence,
    size_t head, size_t width, double scale, bool stable, double *output)
{
    double *scores = calloc(sequence, sizeof(double));
    double *weighted = calloc(sequence, sizeof(double));
    if (!scores || !weighted) { free(scores); free(weighted); return false; }
    double maximum = 0;
    for (size_t row = 0; row < sequence; row++) {
        double dot = reduce_values(q, k + row * head, head, stable);
        scores[row] = stable ? (double)((float)dot * (float)scale) : dot * scale;
        if (!isfinite(scores[row])) { free(scores); free(weighted); return false; }
        if (!row || scores[row] > maximum) maximum = scores[row];
    }
    for (size_t row = 0; row < sequence; row++) {
        double shifted = fmax(fmin(scores[row] - maximum, 30.0), -30.0);
        scores[row] = stable ? (double)expf((float)shifted) : exp(shifted);
    }
    double total = reduce_values(scores, NULL, sequence, stable);
    for (size_t col = 0; col < width; col++) {
        for (size_t row = 0; row < sequence; row++)
            weighted[row] = (scores[row] / total) * v[row * width + col];
        output[col] = reduce_values(weighted, NULL, sequence, stable);
    }
    free(scores);
    free(weighted);
    return true;
}

bool doe_ns_evaluate_attention(const doe_ns_io *io, doe_ns_evaluation *e, char *message, size_t message_size)
{
    const doe_kernel_binding *uniform, *q, *k, *v, *output;
    const doe_command *command = e->command;
    if (!doe_ns_binding_of(command, 0, &uniform, message, message_size) ||
        !doe_ns_binding_of(command, 1, &q, message, message_size) ||
        !doe_ns_binding_of(command, 2, &k, message, message_size) ||
        !doe_ns_binding_of(command, 3, &v, message, message_size) ||
        !doe_ns_binding_of(command, 4, &output, message, message_size)) return false;
    uint8_t *bytes = NULL;
    size_t length = 0;
    if (!doe_ns_capture_uniform(io, uniform, &bytes, &length, message, message_size)) return false;
    size_t sequence = doe_ns_read_u32(bytes, length, 0), head = doe_ns_read_u32(bytes, length, 1);
    double scale = doe_ns_read_f32(bytes, length, 2);
    free(bytes);
    size_t nq, nk, nv, width;
    if (length < 12 || !isfinite(scale) || !sequence || !head || !binding_count(q, &nq) || !binding_count(k, &nk) ||
        !binding_count(v, &nv) || !binding_count(output, &width) || nq != head || nk / head != sequence ||
        nk % head || nv % sequence || nv / sequence != width || width > UINT32_MAX) {
        snprintf(message, message_size, "numeric stability: invalid attention shape");
        return false;
    }
    double *queries = calloc(nq, sizeof(double)), *keys = calloc(nk, sizeof(double)), *values = calloc(nv, sizeof(double));
    double *fast = calloc(width, sizeof(double)), *stable = calloc(width, sizeof(double)), *reference = calloc(width, sizeof(double));
    e->candidates = calloc(width, sizeof(doe_ns_candidate));
    bool ok = queries && keys && values && fast && stable && reference && e->candidates;
    if (ok) ok = doe_ns_capture_f32(io, q->handle, q->buffer_offset, nq, queries, message, message_size) &&
        doe_ns_capture_f32(io, k->handle, k->buffer_offset, nk, keys, message, message_size) &&
        doe_ns_capture_f32(io, v->handle, v->buffer_offset, nv, values, message, message_size) &&
        doe_ns_capture_f32(io, output->handle, output->buffer_offset, width, fast, message, message_size) &&
        attention_values(queries, keys, values, sequence, head, width, scale, true, stable) &&
        attention_values(queries, keys, values, sequence, head, width, scale, false, reference);
    if (ok) {
        e->candidate_count = width;
        for (size_t i = 0; i < width; i++)
            e->candidates[i] = (doe_ns_candidate){.token_id = (uint32_t)i, .fast = fast[i], .stable = stable[i], .reference = reference[i]};
        ok = finite_candidates(e->candidates, width, message, message_size);
        if (!ok) goto attention_cleanup;
        e->fast_token = argmax_token(e->candidates, width, 0);
        e->stable_token = argmax_token(e->candidates, width, 1);
        e->reference_token = argmax_token(e->candidates, width, 2);
        e->logits_handle = output->handle;
        e->logits_offset = output->buffer_offset;
        ok = doe_ns_digest(fast, width, e->fast_digest) && doe_ns_digest(stable, width, e->stable_digest) &&
            doe_ns_layout_fingerprint(command, e->layout_fingerprint);
    }
attention_cleanup:
    free(queries);
    free(keys);
    free(values);
    free(fast);
    free(stable);
    free(reference);
    if (!ok && !message[0]) snprintf(message, message_size, "numeric stability: nonfinite attention evaluation or allocation failure");
    return ok;
}

bool doe_ns_evaluate_rmsnorm(const doe_ns_io *io, doe_ns_evaluation *e, const doe_command *future,
    size_t following, char *message, size_t message_size)
{
    const doe_kernel_binding *uniform, *input, *weight, *output;
    const doe_command *command = e->command, *downstream = NULL;
    if (!doe_ns_binding_of(command, 0, &uniform, message, message_size) ||
        !doe_ns_binding_of(command, 1, &input, message, message_size) ||
        !doe_ns_binding_of(command, 2, &weight, message, message_size) ||
        !doe_ns_binding_of(command, 3, &output, message, message_size)) return false;
    for (size_t i = 0; future && i < following; i++)
        if (future[i].kind == DOE_COMMAND_KERNEL_DISPATCH && future[i].semantic.op_id &&
            !strcmp(future[i].semantic.op_id, "matmul.logits")) { downstream = &future[i]; break; }
    if (!downstream) {
        snprintf(message, message_size, "numeric stability: RMSNorm missing downstream matmul.logits");
        return false;
    }
    /* Without a command effect analysis, intervening commands can mutate either
     * the hidden tensor or downstream weights. Reject rather than guess. */
    if (downstream != future || downstream->numeric) {
        snprintf(message, message_size, "numeric stability: ambiguous RMSNorm downstream chain");
        return false;
    }
    const doe_kernel_binding *matmul_uniform, *weights, *hidden;
    if (!doe_ns_binding_of(downstream, 0, &matmul_uniform, message, message_size) ||
        !doe_ns_binding_of(downstream, 2, &weights, message, message_size) ||
        !doe_ns_binding_of(downstream, 1, &hidden, message, message_size)) return false;
    if (hidden->handle != output->handle || hidden->buffer_offset != output->buffer_offset ||
        hidden->buffer_size != output->buffer_size) {
        snprintf(message, message_size, "numeric stability: RMSNorm downstream hidden identity mismatch");
        return false;
    }
    uint8_t *bytes = NULL;
    size_t length = 0;
    if (!doe_ns_capture_uniform(io, uniform, &bytes, &length, message, message_size)) return false;
    size_t size = doe_ns_read_u32(bytes, length, 0);
    double epsilon = doe_ns_read_f32(bytes, length, 1);
    free(bytes);
    size_t count, weight_count, input_count, downstream_weight_count;
    if (length < 8 || !isfinite(epsilon) || epsilon < 0 || !size || !binding_count(output, &count) || !binding_count(weight, &weight_count) ||
        !binding_count(input, &input_count) || input_count < count || count % size || weight_count < size) {
        snprintf(message, message_size, "numeric stability: invalid RMSNorm shape");
        return false;
    }
    if (!doe_ns_capture_uniform(io, matmul_uniform, &bytes, &length, message, message_size)) return false;
    size_t rows = doe_ns_read_u32(bytes, length, 0), cols = doe_ns_read_u32(bytes, length, 1);
    free(bytes);
    if (length < 8 || !rows || !binding_count(weights, &downstream_weight_count) || downstream_weight_count / rows < count || cols != count || rows > SIZE_MAX / sizeof(doe_ns_candidate) ||
        count > (UINT64_MAX - weights->buffer_offset) / 4 / rows) {
        snprintf(message, message_size, "numeric stability: invalid RMSNorm downstream shape");
        return false;
    }
    double *inputs = calloc(count, sizeof(double)), *scales = calloc(size, sizeof(double));
    double *fast = calloc(count, sizeof(double)), *stable = calloc(count, sizeof(double));
    double *reference = calloc(count, sizeof(double)), *row = calloc(count, sizeof(double));
    e->candidates = calloc(rows, sizeof(doe_ns_candidate));
    bool ok = inputs && scales && fast && stable && reference && row && e->candidates;
    if (ok) ok = doe_ns_capture_f32(io, input->handle, input->buffer_offset, count, inputs, message, message_size) &&
        doe_ns_capture_f32(io, weight->handle, weight->buffer_offset, size, scales, message, message_size) &&
        doe_ns_capture_f32(io, output->handle, output->buffer_offset, count, fast, message, message_size);
    for (size_t start = 0; ok && start < count; start += size) {
        float sum32 = 0;
        double sum64 = 0;
        for (size_t col = 0; col < size; col++) {
            float value = (float)inputs[start + col];
            sum32 = sum32 + value * value;
            sum64 += inputs[start + col] * inputs[start + col];
        }
        if (!isfinite(sum32) || !isfinite(sum64)) {
            snprintf(message, message_size, "numeric stability: nonfinite RMSNorm reduction");
            ok = false;
            break;
        }
        float denominator = (float)((double)sum32 / (double)size + epsilon);
        float rms32 = 1.0f / sqrtf(denominator);
        double rms64 = 1.0 / sqrt(sum64 / (double)size + epsilon);
        for (size_t col = 0; col < size; col++) {
            stable[start + col] = (double)((float)inputs[start + col] * rms32 * (float)scales[col]);
            reference[start + col] = inputs[start + col] * rms64 * scales[col];
        }
    }
    for (size_t i = 0; ok && i < rows; i++) {
        ok = doe_ns_capture_f32(io, weights->handle, weights->buffer_offset + (uint64_t)i * cols * 4,
            count, row, message, message_size);
        if (ok) e->candidates[i] = (doe_ns_candidate){.token_id = (uint32_t)i,
            .fast = reduce_values(fast, row, count, false), .stable = reduce_values(stable, row, count, false),
            .reference = reduce_values(reference, row, count, false)};
    }
    ok = ok && finite_candidates(e->candidates, rows, message, message_size);
    if (ok) {
        e->candidate_count = rows;
        e->fast_token = argmax_token(e->candidates, rows, 0);
        e->stable_token = argmax_token(e->candidates, rows, 1);
        e->reference_token = argmax_token(e->candidates, rows, 2);
        e->logits_handle = output->handle;
        e->logits_offset = output->buffer_offset;
        ok = doe_ns_digest(fast, count, e->fast_digest) && doe_ns_digest(stable, count, e->stable_digest) &&
            doe_ns_layout_fingerprint(command, e->layout_fingerprint);
    }
    if (ok) { e->rewrite_values = stable; e->rewrite_count = count; stable = NULL; }
    free(inputs);
    free(scales);
    free(fast);
    free(stable);
    free(reference);
    free(row);
    if (!ok && !message[0]) snprintf(message, message_size, "numeric stability: out of memory");
    return ok;
}
