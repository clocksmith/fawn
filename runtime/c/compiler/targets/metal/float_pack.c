/* SPDX-License-Identifier: MIT */
#include "private.h"
#include "../../src/builtin.h"

void metal_float_pack_helpers(MetalEmitter *e) {
    bool needed = false;
    for (uint32_t f = 0u; f < e->module->function_count; ++f) {
        if (!metal_function_used(e,f)) continue;
        const FunctionRecord *function = &e->module->functions[f];
        for (uint32_t i = function->instruction_start; i < function->instruction_end; ++i) {
            const IrInstruction *ir = &e->module->instructions[i];
            if (ir->opcode == IR_BUILTIN && ir->operands.scalar.literal >= (uint32_t)BUILTIN_PACK4X8_SNORM &&
                ir->operands.scalar.literal <= (uint32_t)BUILTIN_UNPACK2X16_FLOAT) needed = true;
        }
    }
    if (!needed) return;

    metal_text(e,
        "inline uint doe_pack4x8snorm(float4 v) { uint4 b = uint4(int4(floor(0.5 + clamp(v, -1.0, 1.0) * 127.0))) & 255u; return (b[0] << 0u) | (b[1] << 8u) | (b[2] << 16u) | (b[3] << 24u); }\n"
        "inline float4 doe_unpack4x8snorm(uint v) { return float4(max(-1.0, float(as_type<int>(v << 24u) >> 24u) / 127.0), max(-1.0, float(as_type<int>(v << 16u) >> 24u) / 127.0), max(-1.0, float(as_type<int>(v << 8u) >> 24u) / 127.0), max(-1.0, float(as_type<int>(v << 0u) >> 24u) / 127.0)); }\n"
        "inline uint doe_pack4x8unorm(float4 v) { uint4 b = uint4(int4(floor(0.5 + clamp(v, 0.0, 1.0) * 255.0))) & 255u; return (b[0] << 0u) | (b[1] << 8u) | (b[2] << 16u) | (b[3] << 24u); }\n"
        "inline float4 doe_unpack4x8unorm(uint v) { return float4(float((v >> 0u) & 255u) / 255.0, float((v >> 8u) & 255u) / 255.0, float((v >> 16u) & 255u) / 255.0, float((v >> 24u) & 255u) / 255.0); }\n"
        "inline uint doe_pack2x16snorm(float2 v) { uint2 b = uint2(int2(floor(0.5 + clamp(v, -1.0, 1.0) * 32767.0))) & 65535u; return (b[0] << 0u) | (b[1] << 16u); }\n"
        "inline float2 doe_unpack2x16snorm(uint v) { return float2(max(-1.0, float(as_type<int>(v << 16u) >> 16u) / 32767.0), max(-1.0, float(as_type<int>(v << 0u) >> 16u) / 32767.0)); }\n"
        "inline uint doe_pack2x16unorm(float2 v) { uint2 b = uint2(int2(floor(0.5 + clamp(v, 0.0, 1.0) * 65535.0))) & 65535u; return (b[0] << 0u) | (b[1] << 16u); }\n"
        "inline float2 doe_unpack2x16unorm(uint v) { return float2(float((v >> 0u) & 65535u) / 65535.0, float((v >> 16u) & 65535u) / 65535.0); }\n"
        "inline uint doe_pack2x16float(float2 v) { return as_type<uint>(half2(v)); }\n"
        "inline float2 doe_unpack2x16float(uint v) { return float2(as_type<half2>(v)); }\n");
}
void metal_float_pack_expression(MetalEmitter *e, const IrInstruction *ir, NumericBuiltinKind kind) {
    if (kind == BUILTIN_QUANTIZE_F16) {
        const uint32_t width = type_vector_width(e->module, ir->type);
        metal_type(e, ir->type);
        metal_text(e, "(");
        if (width == 1u) metal_text(e, "half(");
        else metal_format(e, "half%u(", width);
        metal_value(e, ir->operands.scalar.left);
        metal_text(e, "))");
        return;
    }
    const NumericBuiltin *builtin = builtin_kind(kind);
    metal_format(e, "doe_%s(", builtin->name);
    metal_value(e, ir->operands.scalar.left);
    metal_text(e, ")");
}
