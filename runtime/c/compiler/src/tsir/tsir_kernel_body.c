/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "tsir_emit.h"
#include "tsir_format.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* emit_kernel_body.zig, non-CSL backends only, for ops fused_gemv,
 * rms_norm, gather. CSL returns TSIR_ERR_UNSUPPORTED: CSL bodies
 * are quarantined per the 2026-10-26 scope correction and are not
 * built here. All other ops return TSIR_ERR_UNSUPPORTED, exactly
 * as Zig's emitWebGpu/emitMsl/emitDxil/emitSpirV switches do.
 * Backend order matches Zig: webgpu, csl, msl, dxil, spir_v.
 * Literal epsilon and canonical identities share the retained Zig {d}
 * plain-decimal formatter, including extreme exponents and signed zero. */

static void putf(TsirBuffer *buf, const char *fmt, ...) {
    char tmp[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, args);
    va_end(args);
    tsir_buf_puts(buf, tmp);
}

static TsirStatus fail(TsirBuffer *buf, TsirStatus status) {
    if (buf->err == TSIR_OK) {
        buf->err = status;
    }
    return status;
}

static TsirStatus require_roles(const TsirSemanticFunction *func,
    TsirSemanticBindingRole r0, TsirSemanticBindingRole r1,
    TsirSemanticBindingRole r2, const TsirBufferBinding **b0,
    const TsirBufferBinding **b1, const TsirBufferBinding **b2) {
    TsirStatus status = TSIR_OK;
    status = tsir_binding_for_role(func, r0, b0);
    if (status != TSIR_OK) {
        return status;
    }
    status = tsir_binding_for_role(func, r1, b1);
    if (status != TSIR_OK) {
        return status;
    }
    return tsir_binding_for_role(func, r2, b2);
}

static TsirStatus require_gemv(const TsirSemanticFunction *func,
    const TsirBufferBinding **matrix,
    const TsirBufferBinding **vector,
    const TsirBufferBinding **output) {
    TsirStatus status = require_roles(func, TSIR_ROLE_MATRIX,
        TSIR_ROLE_VECTOR, TSIR_ROLE_OUTPUT, matrix, vector, output);
    if (status != TSIR_OK) {
        return status;
    }
    status = tsir_require_elem(*matrix, TSIR_SCALAR_F32);
    if (status != TSIR_OK) {
        return status;
    }
    status = tsir_require_elem(*vector, TSIR_SCALAR_F32);
    if (status != TSIR_OK) {
        return status;
    }
    return tsir_require_elem(*output, TSIR_SCALAR_F32);
}

static TsirStatus require_gather(const TsirSemanticFunction *func,
    const TsirBufferBinding **indices,
    const TsirBufferBinding **table,
    const TsirBufferBinding **output) {
    TsirStatus status = require_roles(func, TSIR_ROLE_INDICES,
        TSIR_ROLE_TABLE, TSIR_ROLE_OUTPUT, indices, table, output);
    if (status != TSIR_OK) {
        return status;
    }
    status = tsir_require_elem(*indices, TSIR_SCALAR_U32);
    if (status != TSIR_OK) {
        return status;
    }
    status = tsir_require_elem(*table, TSIR_SCALAR_F32);
    if (status != TSIR_OK) {
        return status;
    }
    return tsir_require_elem(*output, TSIR_SCALAR_F32);
}

static TsirStatus require_rms(const TsirSemanticFunction *func,
    const TsirBufferBinding **input,
    const TsirBufferBinding **scale,
    const TsirBufferBinding **output,
    const TsirRmsNormBody **rms_out) {
    TsirStatus status = require_roles(func, TSIR_ROLE_INPUT,
        TSIR_ROLE_SCALE, TSIR_ROLE_OUTPUT, input, scale, output);
    if (status != TSIR_OK) {
        return status;
    }
    status = tsir_require_elem(*input, TSIR_SCALAR_F32);
    if (status != TSIR_OK) {
        return status;
    }
    status = tsir_require_elem(*scale, TSIR_SCALAR_F32);
    if (status != TSIR_OK) {
        return status;
    }
    status = tsir_require_elem(*output, TSIR_SCALAR_F32);
    if (status != TSIR_OK) {
        return status;
    }
    if (!func->body.has_rms_norm) {
        return TSIR_ERR_INVALID;
    }
    if (func->body.rms_norm.formula !=
        TSIR_RMSNORM_SUM_SQUARES_MEAN_EPSILON_RSQRT_SCALE) {
        return TSIR_ERR_UNSUPPORTED;
    }
    if (func->body.rms_norm.reduction_target !=
        TSIR_RMSNORM_INTERMEDIATE_SCALAR) {
        return TSIR_ERR_UNSUPPORTED;
    }
    *rms_out = &func->body.rms_norm;
    return TSIR_OK;
}

/* Uniform epsilon validation shared by all backends: Zig returns
 * InvalidBodyContract unless byte_offset is exactly 4 and the
 * uniform binding index resolves. Literal epsilon without a
 * literal value is likewise invalid. */
static TsirStatus check_epsilon(const TsirSemanticFunction *func,
    const TsirRmsNormBody *rms,
    const TsirBufferBinding **uniform_out) {
    *uniform_out = NULL;
    if (rms->epsilon.source == TSIR_EPSILON_UNIFORM_FIELD) {
        TsirStatus status = TSIR_OK;
        if (!rms->epsilon.byte_offset.has ||
            rms->epsilon.byte_offset.value != 4) {
            return TSIR_ERR_INVALID;
        }
        if (!rms->epsilon.binding_index.has) {
            return TSIR_ERR_INVALID;
        }
        status = tsir_binding_for_index(func,
            rms->epsilon.binding_index.value, uniform_out);
        return status;
    }
    if (!rms->epsilon.has_literal_f64) {
        return TSIR_ERR_INVALID;
    }
    return TSIR_OK;
}

static void put_wgsl_epsilon(TsirBuffer *buf,
    const TsirRmsNormBody *rms) {
    if (rms->epsilon.source == TSIR_EPSILON_UNIFORM_FIELD) {
        tsir_buf_puts(buf, "tsir_dims.epsilon");
        return;
    }
    tsir_format_f64(buf, rms->epsilon.literal_f64);
}

static void put_c_epsilon(TsirBuffer *buf,
    const TsirRmsNormBody *rms) {
    if (rms->epsilon.source == TSIR_EPSILON_UNIFORM_FIELD) {
        tsir_buf_puts(buf, "tsir_epsilon");
        return;
    }
    tsir_format_f64(buf, rms->epsilon.literal_f64);
    tsir_buf_putc(buf, 'f');
}

static TsirStatus emit_wgsl_gemv(TsirBuffer *buf,
    const TsirSemanticFunction *func) {
    const TsirBufferBinding *matrix = NULL;
    const TsirBufferBinding *vector = NULL;
    const TsirBufferBinding *output = NULL;
    TsirStatus status = require_gemv(func, &matrix, &vector, &output);
    if (status != TSIR_OK) {
        return fail(buf, status);
    }
    putf(buf,
        "@group(%u) @binding(%u) var<storage, read> tsir_matrix: array<f32>;\n",
        matrix->group, matrix->binding);
    putf(buf,
        "@group(%u) @binding(%u) var<storage, read> tsir_vector: array<f32>;\n",
        vector->group, vector->binding);
    putf(buf,
        "@group(%u) @binding(%u) var<storage, read_write> tsir_output: array<f32>;\n\n",
        output->group, output->binding);
    tsir_buf_puts(buf, "@compute @workgroup_size(1, 1, 1)\n");
    tsir_buf_puts(buf,
        "fn main(@builtin(global_invocation_id) gid: vec3<u32>) {\n");
    tsir_buf_puts(buf, "    let row = gid.x;\n");
    tsir_buf_puts(buf, "    let rows = arrayLength(&tsir_output);\n");
    tsir_buf_puts(buf, "    let cols = arrayLength(&tsir_vector);\n");
    tsir_buf_puts(buf, "    if (row >= rows) {\n");
    tsir_buf_puts(buf, "        return;\n");
    tsir_buf_puts(buf, "    }\n");
    tsir_buf_puts(buf, "    var acc: f32 = 0.0;\n");
    tsir_buf_puts(buf,
        "    for (var k = 0u; k < cols; k = k + 1u) {\n");
    tsir_buf_puts(buf,
        "        acc = acc + tsir_matrix[row * cols + k] * tsir_vector[k];\n");
    tsir_buf_puts(buf, "    }\n");
    tsir_buf_puts(buf, "    tsir_output[row] = acc;\n");
    tsir_buf_puts(buf, "}\n");
    return buf->err;
}

static TsirStatus emit_wgsl_rms(TsirBuffer *buf,
    const TsirSemanticFunction *func) {
    const TsirBufferBinding *input = NULL;
    const TsirBufferBinding *scale = NULL;
    const TsirBufferBinding *output = NULL;
    const TsirBufferBinding *uniform = NULL;
    const TsirRmsNormBody *rms = NULL;
    TsirStatus status = require_rms(func, &input, &scale, &output,
        &rms);
    if (status != TSIR_OK) {
        return fail(buf, status);
    }
    status = check_epsilon(func, rms, &uniform);
    if (status != TSIR_OK) {
        return fail(buf, status);
    }
    if (rms->epsilon.source == TSIR_EPSILON_UNIFORM_FIELD) {
        tsir_buf_puts(buf, "struct TsirRmsNormUniforms {\n");
        tsir_buf_puts(buf, "    hidden_size: u32,\n");
        tsir_buf_puts(buf, "    epsilon: f32,\n");
        tsir_buf_puts(buf, "};\n\n");
    }
    putf(buf,
        "@group(%u) @binding(%u) var<storage, read> tsir_input: array<f32>;\n",
        input->group, input->binding);
    putf(buf,
        "@group(%u) @binding(%u) var<storage, read> tsir_scale: array<f32>;\n",
        scale->group, scale->binding);
    putf(buf,
        "@group(%u) @binding(%u) var<storage, read_write> tsir_output: array<f32>;\n",
        output->group, output->binding);
    if (rms->epsilon.source == TSIR_EPSILON_UNIFORM_FIELD) {
        putf(buf,
            "@group(%u) @binding(%u) var<uniform> tsir_dims: TsirRmsNormUniforms;\n\n",
            uniform->group, uniform->binding);
    } else {
        tsir_buf_puts(buf, "\n");
    }
    tsir_buf_puts(buf, "@compute @workgroup_size(1, 1, 1)\n");
    tsir_buf_puts(buf,
        "fn main(@builtin(global_invocation_id) gid: vec3<u32>) {\n");
    tsir_buf_puts(buf, "    let d = gid.x;\n");
    if (rms->epsilon.source == TSIR_EPSILON_UNIFORM_FIELD) {
        tsir_buf_puts(buf,
            "    let hidden_size = tsir_dims.hidden_size;\n");
    } else {
        tsir_buf_puts(buf,
            "    let hidden_size = arrayLength(&tsir_output);\n");
    }
    tsir_buf_puts(buf, "    if (d >= hidden_size) {\n");
    tsir_buf_puts(buf, "        return;\n");
    tsir_buf_puts(buf, "    }\n");
    tsir_buf_puts(buf, "    var sum_sq: f32 = 0.0;\n");
    tsir_buf_puts(buf,
        "    for (var i = 0u; i < hidden_size; i = i + 1u) {\n");
    tsir_buf_puts(buf, "        let v = tsir_input[i];\n");
    tsir_buf_puts(buf, "        sum_sq = sum_sq + v * v;\n");
    tsir_buf_puts(buf, "    }\n");
    tsir_buf_puts(buf, "    let mean_sq = sum_sq / f32(hidden_size);\n");
    tsir_buf_puts(buf, "    let inv_rms = inverseSqrt(mean_sq + ");
    put_wgsl_epsilon(buf, rms);
    tsir_buf_puts(buf, ");\n");
    tsir_buf_puts(buf,
        "    tsir_output[d] = tsir_input[d] * inv_rms * tsir_scale[d];\n");
    tsir_buf_puts(buf, "}\n");
    return buf->err;
}

static TsirStatus emit_wgsl_gather(TsirBuffer *buf,
    const TsirSemanticFunction *func) {
    const TsirBufferBinding *indices = NULL;
    const TsirBufferBinding *table = NULL;
    const TsirBufferBinding *output = NULL;
    TsirStatus status = require_gather(func, &indices, &table,
        &output);
    if (status != TSIR_OK) {
        return fail(buf, status);
    }
    putf(buf,
        "@group(%u) @binding(%u) var<storage, read> tsir_indices: array<u32>;\n",
        indices->group, indices->binding);
    putf(buf,
        "@group(%u) @binding(%u) var<storage, read> tsir_table: array<f32>;\n",
        table->group, table->binding);
    putf(buf,
        "@group(%u) @binding(%u) var<storage, read_write> tsir_output: array<f32>;\n\n",
        output->group, output->binding);
    tsir_buf_puts(buf, "@compute @workgroup_size(1, 1, 1)\n");
    tsir_buf_puts(buf,
        "fn main(@builtin(global_invocation_id) gid: vec3<u32>) {\n");
    tsir_buf_puts(buf, "    let h = gid.x;\n");
    tsir_buf_puts(buf, "    let token = gid.y;\n");
    tsir_buf_puts(buf, "    let tokens = arrayLength(&tsir_indices);\n");
    tsir_buf_puts(buf,
        "    let hidden = arrayLength(&tsir_output) / tokens;\n");
    tsir_buf_puts(buf, "    if (token >= tokens || h >= hidden) {\n");
    tsir_buf_puts(buf, "        return;\n");
    tsir_buf_puts(buf, "    }\n");
    tsir_buf_puts(buf, "    let row = tsir_indices[token];\n");
    tsir_buf_puts(buf, "    let vocab = arrayLength(&tsir_table) / hidden;\n");
    tsir_buf_puts(buf, "    if (row >= vocab) {\n");
    tsir_buf_puts(buf, "        tsir_output[token * hidden + h] = 0.0;\n");
    tsir_buf_puts(buf, "        return;\n");
    tsir_buf_puts(buf, "    }\n");
    tsir_buf_puts(buf,
        "    tsir_output[token * hidden + h] = tsir_table[row * hidden + h];\n");
    tsir_buf_puts(buf, "}\n");
    return buf->err;
}

static TsirStatus emit_msl_gemv(TsirBuffer *buf,
    const TsirSemanticFunction *func) {
    const TsirBufferBinding *matrix = NULL;
    const TsirBufferBinding *vector = NULL;
    const TsirBufferBinding *output = NULL;
    TsirStatus status = require_gemv(func, &matrix, &vector, &output);
    if (status != TSIR_OK) {
        return fail(buf, status);
    }
    (void)matrix;
    (void)vector;
    (void)output;
    tsir_buf_puts(buf, "#include <metal_stdlib>\nusing namespace metal;\n");
    tsir_buf_puts(buf, "constant uint tsir_M [[function_constant(0)]];\n");
    tsir_buf_puts(buf, "constant uint tsir_K [[function_constant(1)]];\n");
    tsir_buf_puts(buf, "kernel void main0(const device float* tsir_matrix [[buffer(0)]], const device float* tsir_vector [[buffer(1)]], device float* tsir_output [[buffer(2)]], uint row [[thread_position_in_grid]]) {\n");
    tsir_buf_puts(buf, "    if (row >= tsir_M) return;\n");
    tsir_buf_puts(buf, "    float acc = 0.0f;\n");
    tsir_buf_puts(buf, "    for (uint k = 0; k < tsir_K; k++) acc += tsir_matrix[row * tsir_K + k] * tsir_vector[k];\n");
    tsir_buf_puts(buf, "    tsir_output[row] = acc;\n");
    tsir_buf_puts(buf, "}\n");
    return buf->err;
}

static TsirStatus emit_msl_rms(TsirBuffer *buf,
    const TsirSemanticFunction *func) {
    const TsirBufferBinding *input = NULL;
    const TsirBufferBinding *scale = NULL;
    const TsirBufferBinding *output = NULL;
    const TsirRmsNormBody *rms = NULL;
    const TsirBufferBinding *uniform = NULL;
    TsirStatus status = require_rms(func, &input, &scale, &output,
        &rms);
    if (status != TSIR_OK) {
        return fail(buf, status);
    }
    status = check_epsilon(func, rms, &uniform);
    if (status != TSIR_OK) {
        return fail(buf, status);
    }
    (void)input;
    (void)scale;
    (void)output;
    (void)uniform;
    tsir_buf_puts(buf, "#include <metal_stdlib>\nusing namespace metal;\n");
    tsir_buf_puts(buf, "constant uint tsir_hidden_size [[function_constant(0)]];\n");
    tsir_buf_puts(buf, "constant float tsir_epsilon [[function_constant(1)]];\n");
    tsir_buf_puts(buf, "kernel void main0(const device float* tsir_input [[buffer(0)]], const device float* tsir_scale [[buffer(1)]], device float* tsir_output [[buffer(2)]], uint d [[thread_position_in_grid]]) {\n");
    tsir_buf_puts(buf, "    if (d >= tsir_hidden_size) return;\n");
    tsir_buf_puts(buf, "    float sum_sq = 0.0f;\n");
    tsir_buf_puts(buf, "    for (uint i = 0; i < tsir_hidden_size; i++) { float v = tsir_input[i]; sum_sq += v * v; }\n");
    tsir_buf_puts(buf, "    float inv_rms = rsqrt((sum_sq / float(tsir_hidden_size)) + ");
    put_c_epsilon(buf, rms);
    tsir_buf_puts(buf, ");\n");
    tsir_buf_puts(buf, "    tsir_output[d] = tsir_input[d] * inv_rms * tsir_scale[d];\n");
    tsir_buf_puts(buf, "}\n");
    return buf->err;
}

static TsirStatus emit_msl_gather(TsirBuffer *buf,
    const TsirSemanticFunction *func) {
    const TsirBufferBinding *indices = NULL;
    const TsirBufferBinding *table = NULL;
    const TsirBufferBinding *output = NULL;
    TsirStatus status = require_gather(func, &indices, &table,
        &output);
    if (status != TSIR_OK) {
        return fail(buf, status);
    }
    (void)indices;
    (void)table;
    (void)output;
    tsir_buf_puts(buf, "#include <metal_stdlib>\nusing namespace metal;\n");
    tsir_buf_puts(buf, "constant uint tsir_num_tokens [[function_constant(0)]];\n");
    tsir_buf_puts(buf, "constant uint tsir_hidden [[function_constant(1)]];\n");
    tsir_buf_puts(buf, "constant uint tsir_vocab [[function_constant(2)]];\n");
    tsir_buf_puts(buf, "kernel void main0(const device uint* tsir_indices [[buffer(0)]], const device float* tsir_table [[buffer(1)]], device float* tsir_output [[buffer(2)]], uint2 gid [[thread_position_in_grid]]) {\n");
    tsir_buf_puts(buf, "    uint h = gid.x; uint token = gid.y;\n");
    tsir_buf_puts(buf, "    if (token >= tsir_num_tokens || h >= tsir_hidden) return;\n");
    tsir_buf_puts(buf, "    uint row = tsir_indices[token];\n");
    tsir_buf_puts(buf, "    uint dst = token * tsir_hidden + h;\n");
    tsir_buf_puts(buf, "    tsir_output[dst] = (row >= tsir_vocab) ? 0.0f : tsir_table[row * tsir_hidden + h];\n");
    tsir_buf_puts(buf, "}\n");
    return buf->err;
}

static TsirStatus emit_dxil_gemv(TsirBuffer *buf,
    const TsirSemanticFunction *func) {
    const TsirBufferBinding *matrix = NULL;
    const TsirBufferBinding *vector = NULL;
    const TsirBufferBinding *output = NULL;
    TsirStatus status = require_gemv(func, &matrix, &vector, &output);
    if (status != TSIR_OK) {
        return fail(buf, status);
    }
    (void)matrix;
    (void)vector;
    (void)output;
    tsir_buf_puts(buf, "StructuredBuffer<float> tsir_matrix : register(t0);\n");
    tsir_buf_puts(buf, "StructuredBuffer<float> tsir_vector : register(t1);\n");
    tsir_buf_puts(buf, "RWStructuredBuffer<float> tsir_output : register(u2);\n");
    tsir_buf_puts(buf, "cbuffer TsirDims : register(b0) { uint tsir_M; uint tsir_K; };\n");
    tsir_buf_puts(buf, "[numthreads(1, 1, 1)] void main(uint3 gid : SV_DispatchThreadID) {\n");
    tsir_buf_puts(buf, "    uint row = gid.x; if (row >= tsir_M) return;\n");
    tsir_buf_puts(buf, "    float acc = 0.0f;\n");
    tsir_buf_puts(buf, "    for (uint k = 0; k < tsir_K; k++) acc += tsir_matrix[row * tsir_K + k] * tsir_vector[k];\n");
    tsir_buf_puts(buf, "    tsir_output[row] = acc;\n");
    tsir_buf_puts(buf, "}\n");
    return buf->err;
}

static TsirStatus emit_dxil_rms(TsirBuffer *buf,
    const TsirSemanticFunction *func) {
    const TsirBufferBinding *input = NULL;
    const TsirBufferBinding *scale = NULL;
    const TsirBufferBinding *output = NULL;
    const TsirRmsNormBody *rms = NULL;
    const TsirBufferBinding *uniform = NULL;
    TsirStatus status = require_rms(func, &input, &scale, &output,
        &rms);
    if (status != TSIR_OK) {
        return fail(buf, status);
    }
    status = check_epsilon(func, rms, &uniform);
    if (status != TSIR_OK) {
        return fail(buf, status);
    }
    (void)input;
    (void)scale;
    (void)output;
    (void)uniform;
    tsir_buf_puts(buf, "StructuredBuffer<float> tsir_input : register(t0);\n");
    tsir_buf_puts(buf, "StructuredBuffer<float> tsir_scale : register(t1);\n");
    tsir_buf_puts(buf, "RWStructuredBuffer<float> tsir_output : register(u2);\n");
    tsir_buf_puts(buf, "cbuffer TsirDims : register(b0) { uint tsir_hidden_size; float tsir_epsilon; };\n");
    tsir_buf_puts(buf, "[numthreads(1, 1, 1)] void main(uint3 gid : SV_DispatchThreadID) {\n");
    tsir_buf_puts(buf, "    uint d = gid.x; if (d >= tsir_hidden_size) return;\n");
    tsir_buf_puts(buf, "    float sum_sq = 0.0f;\n");
    tsir_buf_puts(buf, "    for (uint i = 0; i < tsir_hidden_size; i++) { float v = tsir_input[i]; sum_sq += v * v; }\n");
    tsir_buf_puts(buf, "    float inv_rms = rsqrt((sum_sq / float(tsir_hidden_size)) + ");
    put_c_epsilon(buf, rms);
    tsir_buf_puts(buf, ");\n");
    tsir_buf_puts(buf, "    tsir_output[d] = tsir_input[d] * inv_rms * tsir_scale[d];\n");
    tsir_buf_puts(buf, "}\n");
    return buf->err;
}

static TsirStatus emit_dxil_gather(TsirBuffer *buf,
    const TsirSemanticFunction *func) {
    const TsirBufferBinding *indices = NULL;
    const TsirBufferBinding *table = NULL;
    const TsirBufferBinding *output = NULL;
    TsirStatus status = require_gather(func, &indices, &table,
        &output);
    if (status != TSIR_OK) {
        return fail(buf, status);
    }
    (void)indices;
    (void)table;
    (void)output;
    tsir_buf_puts(buf, "StructuredBuffer<uint> tsir_indices : register(t0);\n");
    tsir_buf_puts(buf, "StructuredBuffer<float> tsir_table : register(t1);\n");
    tsir_buf_puts(buf, "RWStructuredBuffer<float> tsir_output : register(u2);\n");
    tsir_buf_puts(buf, "cbuffer TsirDims : register(b0) { uint tsir_num_tokens; uint tsir_hidden; uint tsir_vocab; };\n");
    tsir_buf_puts(buf, "[numthreads(1, 1, 1)] void main(uint3 gid : SV_DispatchThreadID) {\n");
    tsir_buf_puts(buf, "    uint h = gid.x; uint token = gid.y;\n");
    tsir_buf_puts(buf, "    if (token >= tsir_num_tokens || h >= tsir_hidden) return;\n");
    tsir_buf_puts(buf, "    uint row = tsir_indices[token]; uint dst = token * tsir_hidden + h;\n");
    tsir_buf_puts(buf, "    tsir_output[dst] = (row >= tsir_vocab) ? 0.0f : tsir_table[row * tsir_hidden + h];\n");
    tsir_buf_puts(buf, "}\n");
    return buf->err;
}

static TsirStatus emit_spirv_gemv(TsirBuffer *buf,
    const TsirSemanticFunction *func) {
    const TsirBufferBinding *matrix = NULL;
    const TsirBufferBinding *vector = NULL;
    const TsirBufferBinding *output = NULL;
    TsirStatus status = require_gemv(func, &matrix, &vector, &output);
    if (status != TSIR_OK) {
        return fail(buf, status);
    }
    (void)matrix;
    (void)vector;
    (void)output;
    tsir_buf_puts(buf, "#version 450\nlayout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;\n");
    tsir_buf_puts(buf, "layout(set = 0, binding = 0) readonly buffer TsirMatrix { float tsir_matrix[]; };\n");
    tsir_buf_puts(buf, "layout(set = 0, binding = 1) readonly buffer TsirVector { float tsir_vector[]; };\n");
    tsir_buf_puts(buf, "layout(set = 0, binding = 2) buffer TsirOutput { float tsir_output[]; };\n");
    tsir_buf_puts(buf, "layout(set = 0, binding = 3) uniform TsirDims { uint tsir_M; uint tsir_K; };\n");
    tsir_buf_puts(buf, "void main() { uint row = gl_GlobalInvocationID.x; if (row >= tsir_M) return; float acc = 0.0; for (uint k = 0; k < tsir_K; k++) acc += tsir_matrix[row * tsir_K + k] * tsir_vector[k]; tsir_output[row] = acc; }\n");
    return buf->err;
}

static TsirStatus emit_spirv_rms(TsirBuffer *buf,
    const TsirSemanticFunction *func) {
    const TsirBufferBinding *input = NULL;
    const TsirBufferBinding *scale = NULL;
    const TsirBufferBinding *output = NULL;
    const TsirRmsNormBody *rms = NULL;
    const TsirBufferBinding *uniform = NULL;
    TsirStatus status = require_rms(func, &input, &scale, &output,
        &rms);
    if (status != TSIR_OK) {
        return fail(buf, status);
    }
    status = check_epsilon(func, rms, &uniform);
    if (status != TSIR_OK) {
        return fail(buf, status);
    }
    (void)input;
    (void)scale;
    (void)output;
    (void)uniform;
    tsir_buf_puts(buf, "#version 450\nlayout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;\n");
    tsir_buf_puts(buf, "layout(set = 0, binding = 0) readonly buffer TsirInput { float tsir_input[]; };\n");
    tsir_buf_puts(buf, "layout(set = 0, binding = 1) readonly buffer TsirScale { float tsir_scale[]; };\n");
    tsir_buf_puts(buf, "layout(set = 0, binding = 2) buffer TsirOutput { float tsir_output[]; };\n");
    tsir_buf_puts(buf, "layout(set = 0, binding = 3) uniform TsirDims { uint tsir_hidden_size; float tsir_epsilon; };\n");
    tsir_buf_puts(buf, "void main() { uint d = gl_GlobalInvocationID.x; if (d >= tsir_hidden_size) return; float sum_sq = 0.0; for (uint i = 0; i < tsir_hidden_size; i++) { float v = tsir_input[i]; sum_sq += v * v; } float inv_rms = inversesqrt((sum_sq / float(tsir_hidden_size)) + ");
    put_c_epsilon(buf, rms);
    tsir_buf_puts(buf, "); tsir_output[d] = tsir_input[d] * inv_rms * tsir_scale[d]; }\n");
    return buf->err;
}

static TsirStatus emit_spirv_gather(TsirBuffer *buf,
    const TsirSemanticFunction *func) {
    const TsirBufferBinding *indices = NULL;
    const TsirBufferBinding *table = NULL;
    const TsirBufferBinding *output = NULL;
    TsirStatus status = require_gather(func, &indices, &table,
        &output);
    if (status != TSIR_OK) {
        return fail(buf, status);
    }
    (void)indices;
    (void)table;
    (void)output;
    tsir_buf_puts(buf, "#version 450\nlayout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;\n");
    tsir_buf_puts(buf, "layout(set = 0, binding = 0) readonly buffer TsirIndices { uint tsir_indices[]; };\n");
    tsir_buf_puts(buf, "layout(set = 0, binding = 1) readonly buffer TsirTable { float tsir_table[]; };\n");
    tsir_buf_puts(buf, "layout(set = 0, binding = 2) buffer TsirOutput { float tsir_output[]; };\n");
    tsir_buf_puts(buf, "layout(set = 0, binding = 3) uniform TsirDims { uint tsir_num_tokens; uint tsir_hidden; uint tsir_vocab; };\n");
    tsir_buf_puts(buf, "void main() { uint h = gl_GlobalInvocationID.x; uint token = gl_GlobalInvocationID.y; if (token >= tsir_num_tokens || h >= tsir_hidden) return; uint row = tsir_indices[token]; uint dst = token * tsir_hidden + h; tsir_output[dst] = (row >= tsir_vocab) ? 0.0 : tsir_table[row * tsir_hidden + h]; }\n");
    return buf->err;
}

TsirStatus tsir_emit_kernel_body(const TsirSemanticFunction *func,
    int backend, TsirBuffer *buf) {
    if (buf == NULL) {
        return TSIR_ERR_INVALID;
    }
    if (func == NULL) {
        return fail(buf, TSIR_ERR_INVALID);
    }
    if (backend == TSIR_KERNEL_CSL) {
        /* Quarantined: CSL bodies are not built in this directory. */
        return fail(buf, TSIR_ERR_UNSUPPORTED);
    }
    if (backend != TSIR_KERNEL_WEBGPU &&
        backend != TSIR_KERNEL_MSL &&
        backend != TSIR_KERNEL_DXIL &&
        backend != TSIR_KERNEL_SPIR_V) {
        return fail(buf, TSIR_ERR_INVALID);
    }
    switch (func->body.op) {
        case TSIR_BODY_FUSED_GEMV:
            if (backend == TSIR_KERNEL_WEBGPU) {
                return emit_wgsl_gemv(buf, func);
            }
            if (backend == TSIR_KERNEL_MSL) {
                return emit_msl_gemv(buf, func);
            }
            if (backend == TSIR_KERNEL_DXIL) {
                return emit_dxil_gemv(buf, func);
            }
            return emit_spirv_gemv(buf, func);
        case TSIR_BODY_RMS_NORM:
            if (backend == TSIR_KERNEL_WEBGPU) {
                return emit_wgsl_rms(buf, func);
            }
            if (backend == TSIR_KERNEL_MSL) {
                return emit_msl_rms(buf, func);
            }
            if (backend == TSIR_KERNEL_DXIL) {
                return emit_dxil_rms(buf, func);
            }
            return emit_spirv_rms(buf, func);
        case TSIR_BODY_GATHER:
            if (backend == TSIR_KERNEL_WEBGPU) {
                return emit_wgsl_gather(buf, func);
            }
            if (backend == TSIR_KERNEL_MSL) {
                return emit_msl_gather(buf, func);
            }
            if (backend == TSIR_KERNEL_DXIL) {
                return emit_dxil_gather(buf, func);
            }
            return emit_spirv_gather(buf, func);
        case TSIR_BODY_UNKNOWN:
        case TSIR_BODY_RESIDUAL_ADD:
        case TSIR_BODY_GELU_GATED:
        case TSIR_BODY_SILU_GATED:
        case TSIR_BODY_SIGMOID_GATED:
        case TSIR_BODY_KV_WRITE:
        case TSIR_BODY_KV_READ:
        case TSIR_BODY_ATTENTION_SCORES:
        case TSIR_BODY_CONV1D_DEPTHWISE:
        case TSIR_BODY_L2_NORMALIZE:
        case TSIR_BODY_LINEAR_ATTENTION:
            return fail(buf, TSIR_ERR_UNSUPPORTED);
    }
    return fail(buf, TSIR_ERR_UNSUPPORTED);
}
