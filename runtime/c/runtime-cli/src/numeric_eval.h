/* SPDX-License-Identifier: MIT */
#ifndef DOE_RUNTIME_NUMERIC_EVAL_H
#define DOE_RUNTIME_NUMERIC_EVAL_H
#include "numeric_stability.h"
bool doe_ns_binding_of(const doe_command *command, uint32_t index, const doe_kernel_binding **out,
    char *message, size_t message_size);
bool doe_ns_capture_f32(const doe_ns_io *io, uint64_t handle, uint64_t offset, size_t count, double *out,
    char *message, size_t message_size);
bool doe_ns_capture_uniform(const doe_ns_io *io, const doe_kernel_binding *binding, uint8_t **bytes,
    size_t *length, char *message, size_t message_size);
uint32_t doe_ns_read_u32(const uint8_t *bytes, size_t length, size_t word);
float doe_ns_read_f32(const uint8_t *bytes, size_t length, size_t word);
bool doe_ns_evaluate_matmul(const doe_ns_io *io, doe_ns_evaluation *evaluation, bool decode,
    char *message, size_t message_size);
bool doe_ns_evaluate_attention(const doe_ns_io *io, doe_ns_evaluation *evaluation,
    char *message, size_t message_size);
bool doe_ns_evaluate_rmsnorm(const doe_ns_io *io, doe_ns_evaluation *evaluation,
    const doe_command *future, size_t following, char *message, size_t message_size);
#endif
