/* SPDX-License-Identifier: MIT */
#include "sema.h"
#include <math.h>
#include <string.h>

static bool floating(Source *s, uint32_t offset, double number, IrType type, NumericValue *out) {
    uint64_t bits;
    memcpy(&bits, &number, sizeof(bits));
    return numeric_convert(s, offset, (NumericValue){bits,IR_ABSTRACT_FLOAT}, type, out);
}
static bool scalar(Source *s, uint32_t offset, NumericBuiltinKind kind,
    NumericValue input, NumericValue exponent, NumericValue result[2]) {
    NumericValue wide;
    if (!numeric_convert(s, offset, input, IR_ABSTRACT_FLOAT, &wide)) return false;
    double number, fraction, whole;
    memcpy(&number, &wide.bits, sizeof(number));
    if (kind == BUILTIN_LDEXP) {
        if (!numeric_convert(s, offset, exponent, IR_ABSTRACT_INT, &wide)) return false;
        int64_t power;
        memcpy(&power, &wide.bits, sizeof(power));
        const int64_t limit = input.type == IR_F16 ? 16 : input.type == IR_F32 ? 128 : 1024;
        if (power > limit)
            return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, offset, "ldexp exponent exceeds its floating type range");
        return floating(s, offset, power < -1075 ? copysign(0.0,number) : ldexp(number,(int)power), input.type, &result[0]);
    }
    if (kind == BUILTIN_FREXP) {
        int power;
        fraction = frexp(number,&power);
        result[1] = (NumericValue){(uint64_t)(int64_t)power,
            input.type == IR_ABSTRACT_FLOAT ? IR_ABSTRACT_INT : IR_I32};
        if (result[1].type == IR_I32) result[1].bits &= UINT32_MAX;
    } else {
        fraction = modf(number,&whole);
        if (!floating(s, offset, whole, input.type, &result[1])) return false;
    }
    return floating(s, offset, fraction, input.type, &result[0]);
}
bool decompose_constant(Source *s, uint32_t offset, NumericBuiltinKind kind,
    const ConstantValue *arguments, TypeId result_type, ConstantValue *out) {
    const uint32_t width = type_vector_width(s->compiler, arguments[0].type);
    ConstantValue fields[2], lanes[2][4];
    for (uint32_t i = 0u; i < width; ++i) {
        ConstantValue input = arguments[0], exponent = {0};
        if (width != 1u && !constant_project(s, input, i, &input)) return false;
        if (kind == BUILTIN_LDEXP) {
            exponent = arguments[1];
            if (width != 1u && !constant_project(s, exponent, i, &exponent)) return false;
        }
        NumericValue values[2];
        if (!scalar(s, offset, kind, (NumericValue){input.bits,(IrType)input.type},
            (NumericValue){exponent.bits,(IrType)exponent.type}, values)) return false;
        for (uint32_t field = 0u; field < (kind == BUILTIN_LDEXP ? 1u : 2u); ++field)
            lanes[field][i] = (ConstantValue){.type=(TypeId)values[field].type,.bits=values[field].bits};
    }
    for (uint32_t field = 0u; field < (kind == BUILTIN_LDEXP ? 1u : 2u); ++field) {
        const TypeId type = kind == BUILTIN_LDEXP ? result_type : type_child(s->compiler,result_type,field);
        if (width == 1u) fields[field] = lanes[field][0];
        else if (!constant_composite(s,type,lanes[field],width,false,&fields[field])) return false;
    }
    if (kind == BUILTIN_LDEXP) { *out = fields[0]; return true; }
    return constant_composite(s,result_type,fields,2u,false,out);
}
