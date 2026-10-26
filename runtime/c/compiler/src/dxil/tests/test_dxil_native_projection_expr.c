/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
/* Typed-view controls, not native DXIL semantic or device qualification. */
#include "dxil_analyze.h"
#include "test_util.h"
#include <errno.h>
#include <fenv.h>

static DxilIrModule module;
static char names[65536];

static bool analyze(const char *source) {
    DxilDiagnostic diagnostic;
    DxilDiagKind error = DXIL_DIAG_INVALID_IR;
    bool ok = dxil_analyze(source, strlen(source), &module, names, sizeof(names),
        &diagnostic, &error);
    CHECK_MSG(ok, "canonical projection failed: %s; source: %.80s", diagnostic.message, source);
    return ok;
}

static DxilIrFunction *function_named(const char *name) {
    for (uint32_t i = 0u; i < module.function_count; ++i) {
        if (strcmp(module.functions[i].name, name) == 0) return &module.functions[i];
    }
    CHECK(false);
    return NULL;
}

static void check_ranges(void) {
    for (uint32_t f = 0u; f < module.function_count; ++f) {
        const DxilIrFunction *function = &module.functions[f];
        for (uint32_t i = 0u; i < function->expr_count; ++i) {
            const DxilIrExpr *expression = &function->exprs[i];
            CHECK(expression->ty < module.type_count);
            if (expression->kind != DXIL_IR_EXPR_CALL &&
                expression->kind != DXIL_IR_EXPR_CONSTRUCT) continue;
            CHECK(expression->args.start <= function->expr_arg_count);
            CHECK(expression->args.len <= function->expr_arg_count - expression->args.start);
            for (uint32_t a = 0u; a < expression->args.len; ++a) {
                CHECK(function->expr_args[expression->args.start + a] < function->expr_count);
            }
        }
    }
}

static void check_nested_arguments(void) {
    if (!analyze("fn make(x:u32)->vec2u { return vec2<u32>(max(x,3u),min(4u,5u)); }"
        "@compute @workgroup_size(1) fn main(){let v=make(7u);}")) return;
    DxilIrFunction *function = function_named("make");
    if (function == NULL) return;
    unsigned constructors = 0u;
    for (uint32_t i = 0u; i < function->expr_count; ++i) {
        const DxilIrExpr *expression = &function->exprs[i];
        if (expression->kind != DXIL_IR_EXPR_CONSTRUCT) continue;
        ++constructors;
        CHECK(expression->args.len == 2u);
        const DxilIrExpr *left = &function->exprs[function->expr_args[expression->args.start]];
        const DxilIrExpr *right = &function->exprs[function->expr_args[expression->args.start + 1u]];
        CHECK(left->kind == DXIL_IR_EXPR_CALL && strcmp(left->name, "max") == 0);
        CHECK(right->kind == DXIL_IR_EXPR_CALL && strcmp(right->name, "min") == 0);
        CHECK(left->args.len == 2u && right->args.len == 2u);
    }
    CHECK(constructors == 1u);
    function = function_named("main");
    if (function == NULL) return;
    unsigned users = 0u;
    for (uint32_t i = 0u; i < function->expr_count; ++i) {
        const DxilIrExpr *expression = &function->exprs[i];
        if (expression->kind == DXIL_IR_EXPR_CALL && strcmp(expression->name, "make") == 0) {
            CHECK(expression->call_kind == DXIL_IR_CALL_USER);
            ++users;
        }
    }
    CHECK(users == 1u);
    check_ranges();
}

static void check_shadowing(void) {
    if (!analyze("fn project(x:u32)->u32 {var y=x; {let x=9u; y=x;} return y;}"
        "@compute @workgroup_size(1) fn main(){let result=project(1u);}")) return;
    DxilIrFunction *function = function_named("project");
    if (function == NULL) return;
    CHECK(function->local_count == 2u);
    bool shadow = false, parameter = false;
    for (uint32_t i = 0u; i < function->expr_count; ++i) {
        const DxilIrExpr *expression = &function->exprs[i];
        if (expression->kind == DXIL_IR_EXPR_LOCAL_REF && expression->index == 1u) shadow = true;
        if (expression->kind == DXIL_IR_EXPR_PARAM_REF && expression->index == 0u) parameter = true;
    }
    CHECK(shadow && parameter);
    check_ranges();
}

static void check_operators(void) {
    if (!analyze("fn calculate(a:u32,b:u32)->bool {"
        "let arithmetic=((a+b)-(a*b))/(b+1u)%(b+2u);"
        "let bits=((a&b)|(a^b))<<(b%32u); let shifted=bits>>(a%32u);"
        "let negative=-i32(a); let complement=~a;"
        "return (!(a==b)&&(a!=b))||(a<b)||(a<=b)||(a>b)||(a>=b); }"
        "@compute @workgroup_size(1) fn main(){let result=calculate(3u,4u);}")) return;
    DxilIrFunction *function = function_named("calculate");
    if (function == NULL) return;
    bool binary_seen[DXIL_IR_BINARY_LOGICAL_OR + 1] = {false};
    bool unary_seen[DXIL_IR_UNARY_BIT_NOT + 1] = {false};
    for (uint32_t i = 0u; i < function->expr_count; ++i) {
        const DxilIrExpr *expression = &function->exprs[i];
        if (expression->kind == DXIL_IR_EXPR_BINARY) binary_seen[expression->binary_op] = true;
        if (expression->kind == DXIL_IR_EXPR_UNARY) unary_seen[expression->unary_op] = true;
    }
    for (size_t i = 0u; i < sizeof(binary_seen) / sizeof(binary_seen[0]); ++i) CHECK(binary_seen[i]);
    for (size_t i = 0u; i < sizeof(unary_seen) / sizeof(unary_seen[0]); ++i) CHECK(unary_seen[i]);
    check_ranges();
}

static void check_access_categories(void) {
    if (!analyze("struct S {v:vec4f,m:mat2x2f}"
        "@compute @workgroup_size(1) fn main(){var s:S; let pair=s.v.xy;"
        "let element=s.v.z; let column=s.m[1u]; let lane=s.m[1u][0u];}")) return;
    DxilIrFunction *function = function_named("main");
    if (function == NULL) return;
    bool pair = false, scalar = false, column = false, lane = false;
    for (uint32_t i = 0u; i < function->expr_count; ++i) {
        const DxilIrExpr *expression = &function->exprs[i];
        if (expression->kind == DXIL_IR_EXPR_MEMBER && strcmp(expression->field_name, "xy") == 0) {
            pair = true;
            CHECK(expression->category == DXIL_IR_EXPR_VALUE);
            CHECK(function->exprs[expression->base].kind == DXIL_IR_EXPR_LOAD);
        }
        if (expression->kind == DXIL_IR_EXPR_MEMBER && strcmp(expression->field_name, "z") == 0) {
            scalar = true;
            CHECK(expression->category == DXIL_IR_EXPR_REF && expression->field_index == 2u);
        }
        if (expression->kind == DXIL_IR_EXPR_INDEX) {
            const DxilIrType *type = &module.types[expression->ty];
            column = column || (type->kind == DXIL_IR_TYPE_VECTOR && type->len == 2u);
            lane = lane || (type->kind == DXIL_IR_TYPE_SCALAR && type->scalar == DXIL_IR_SCALAR_F32);
        }
    }
    CHECK(pair && scalar && column && lane);
    check_ranges();
}

static void check_pointer_arguments(void) {
    if (!analyze("var<workgroup> counter:atomic<u32>;"
        "@group(0) @binding(0) var<storage,read_write> data:array<u32>;"
        "@compute @workgroup_size(1) fn main(){let old=atomicAdd(&counter,1u);"
        "let count=arrayLength(&data); var x=1u; let p=&x; *p=2u; let z=*p;}")) return;
    DxilIrFunction *function = function_named("main");
    if (function == NULL) return;
    unsigned reference_calls = 0u, addresses = 0u, dereferences = 0u;
    for (uint32_t i = 0u; i < function->expr_count; ++i) {
        const DxilIrExpr *expression = &function->exprs[i];
        if (expression->kind == DXIL_IR_EXPR_CALL &&
            (strcmp(expression->name, "atomicAdd") == 0 || strcmp(expression->name, "arrayLength") == 0)) {
            const DxilIrExpr *argument = &function->exprs[function->expr_args[expression->args.start]];
            CHECK(argument->kind == DXIL_IR_EXPR_GLOBAL_REF);
            CHECK(argument->category == DXIL_IR_EXPR_REF);
            ++reference_calls;
        }
        if (expression->kind == DXIL_IR_EXPR_ADDRESS_OF) ++addresses;
        if (expression->kind == DXIL_IR_EXPR_DEREF) {
            ++dereferences;
            CHECK(expression->category == DXIL_IR_EXPR_REF);
        }
    }
    CHECK(reference_calls == 2u && addresses == 1u && dereferences == 2u);
    check_ranges();
}

static void check_literals_and_conversions(void) {
    int saved_round = fegetround();
    CHECK(fesetround(FE_DOWNWARD) == 0);
    errno = ERANGE;
    bool ok = analyze("enable f16; @compute @workgroup_size(1) fn main(){"
        "let x=0.1000000001f; let h=0.3333h; let bits=bitcast<u32>(x); let y=ldexp(2,3);}");
    CHECK(errno == ERANGE);
    CHECK(fegetround() == FE_DOWNWARD);
    CHECK(fesetround(saved_round) == 0);
    if (!ok) return;
    DxilIrFunction *function = function_named("main");
    if (function == NULL) return;
    bool floating = false, half = false, conversion = false, bitcast = false;
    for (uint32_t i = 0u; i < function->expr_count; ++i) {
        const DxilIrExpr *expression = &function->exprs[i];
        if (expression->kind == DXIL_IR_EXPR_FLOAT_LIT) {
            floating = floating || expression->float_value == 0.1000000001;
            half = half || expression->float_value == 0.3333;
        }
        if (expression->kind == DXIL_IR_EXPR_CALL && strcmp(expression->name, "ldexp") == 0) {
            const DxilIrExpr *first = &function->exprs[function->expr_args[expression->args.start]];
            conversion = first->kind == DXIL_IR_EXPR_CONSTRUCT && first->ty == expression->ty;
        }
        if (expression->kind == DXIL_IR_EXPR_CALL && strcmp(expression->name, "bitcast") == 0)
            bitcast = module.types[expression->ty].scalar == DXIL_IR_SCALAR_U32;
    }
    CHECK(floating && half && conversion && bitcast);
    check_ranges();
}

static void check_packed_lowering(void) {
    if (!analyze("@compute @workgroup_size(1) fn main(){const word=1+2;"
        "let a=unpack4xU8(word); let b=unpack4xI8(0xFFFFFFFF);"
        "let c=pack4xU8(vec4u(1u,2u,3u,4u));"
        "let d=pack4xI8Clamp(vec4i(-200,2,3,400)); let e=dot4U8Packed(word,4);}")) return;
    DxilIrFunction *function = function_named("main");
    if (function == NULL) return;
    unsigned dots = 0u, masks = 0u;
    bool materialized = false;
    for (uint32_t i = 0u; i < function->expr_count; ++i) {
        const DxilIrExpr *expression = &function->exprs[i];
        if (expression->kind == DXIL_IR_EXPR_CALL) {
            CHECK(strncmp(expression->name, "pack4x", 6u) != 0);
            CHECK(strncmp(expression->name, "unpack4x", 8u) != 0);
            CHECK(strncmp(expression->name, "dot4", 4u) != 0);
            if (strcmp(expression->name, "dot") == 0) ++dots;
        }
        if (expression->kind == DXIL_IR_EXPR_BINARY && expression->binary_op == DXIL_IR_BINARY_BIT_AND) ++masks;
        if (expression->kind == DXIL_IR_EXPR_INT_LIT && expression->uint_value == 3u &&
            module.types[expression->ty].kind == DXIL_IR_TYPE_SCALAR &&
            module.types[expression->ty].scalar == DXIL_IR_SCALAR_U32) materialized = true;
    }
    CHECK(dots == 3u && masks >= 3u && materialized);
    check_ranges();
}

static void check_bounds_graph(void) {
    if (!analyze("@group(0) @binding(0) var<storage,read> data:array<u32>;"
        "@group(0) @binding(1) var<storage,read> fixed:array<u32,4>;"
        "@compute @workgroup_size(1) fn main(@builtin(global_invocation_id) gid:vec3u){"
        "var local:array<u32,4>; let a=local[1u]; let b=local[gid.x];"
        "let c=data[gid.x]; let d=fixed[1u];}")) return;
    DxilIrFunction *function = function_named("main");
    if (function == NULL) return;
    unsigned clamped = 0u, unclamped = 0u, runtime = 0u;
    for (uint32_t i = 0u; i < function->expr_count; ++i) {
        const DxilIrExpr *expression = &function->exprs[i];
        if (expression->kind != DXIL_IR_EXPR_INDEX) continue;
        const DxilIrExpr *index = &function->exprs[expression->index_expr];
        if (index->kind != DXIL_IR_EXPR_CALL) { ++unclamped; continue; }
        ++clamped;
        CHECK(strcmp(index->name, "min") == 0 && index->robustness_generated);
        CHECK(index->args.len == 2u);
        const DxilIrExpr *maximum = &function->exprs[function->expr_args[index->args.start + 1u]];
        if (maximum->kind == DXIL_IR_EXPR_BINARY) {
            ++runtime;
            CHECK(maximum->binary_op == DXIL_IR_BINARY_SUB);
            const DxilIrExpr *length = &function->exprs[maximum->lhs];
            CHECK(length->kind == DXIL_IR_EXPR_CALL && strcmp(length->name, "arrayLength") == 0);
            CHECK(length->args.len == 1u && function->expr_args[length->args.start] == expression->base);
            CHECK(function->exprs[maximum->rhs].kind == DXIL_IR_EXPR_INT_LIT &&
                function->exprs[maximum->rhs].uint_value == 1u);
        } else CHECK(maximum->kind == DXIL_IR_EXPR_INT_LIT && maximum->uint_value == 3u);
    }
    CHECK(clamped == 3u && unclamped == 1u && runtime == 1u);
    check_ranges();
}

static unsigned clamp_count(const DxilIrFunction *function) {
    unsigned count = 0u;
    for (uint32_t i = 0u; i < function->expr_count; ++i) {
        const DxilIrExpr *expression = &function->exprs[i];
        if (expression->kind == DXIL_IR_EXPR_INDEX &&
            function->exprs[expression->index_expr].kind == DXIL_IR_EXPR_CALL &&
            function->exprs[expression->index_expr].robustness_generated) ++count;
    }
    return count;
}
static void check_guarded_bounds(void) {
    if (analyze("@compute @workgroup_size(1) fn main(){var values:array<u32,4>;"
        "for(var i=0u;i<4u;i++){let n=values[i];}"
        "for(var j=0u;3u>=j;j+=1u){let n=values[j];}"
        "var k=0u;for(k=1u;k<=3u;k=k+1u){let n=values[k];}}")) {
        DxilIrFunction *function = function_named("main");
        if (function != NULL) CHECK(clamp_count(function) == 0u);
        check_ranges();
    }
    if (analyze("@compute @workgroup_size(1) fn main(){var values:array<u32,4>;"
        "for(var i=0u;i<4u;i++){i=9u;let n=values[i];}"
        "for(var j=0u;j<4u;j++){let j=8u;let n=values[j];}"
        "for(var k=0u;k<5u;k++){let n=values[k];}}")) {
        DxilIrFunction *function = function_named("main");
        if (function != NULL) CHECK(clamp_count(function) == 3u);
        check_ranges();
    }
    if (analyze("@compute @workgroup_size(1) fn main(){var values:array<u32,4>;"
        "var i=0u;let p=&i;for(i=0u;i<4u;i++){*p=8u;let n=values[i];}}")) {
        DxilIrFunction *function = function_named("main");
        if (function != NULL) CHECK(clamp_count(function) == 1u);
        check_ranges();
    }
    if (analyze("const N=100u;@compute @workgroup_size(1) fn main(){var values:array<u32,4>;"
        "for(var i=0u;i<N;i++){const N=4u;let n=values[i];}}")) {
        DxilIrFunction *function = function_named("main");
        if (function != NULL) CHECK(clamp_count(function) == 1u);
    }
    if (analyze("@compute @workgroup_size(1) fn main(){var values:array<u32,4>;"
        "var i=1u;let p=&i;let n=values[*p];}")) {
        DxilIrFunction *function = function_named("main");
        if (function != NULL) CHECK(clamp_count(function) == 1u);
    }
}

int main(void) {
    check_nested_arguments();
    check_shadowing();
    check_operators();
    check_access_categories();
    check_pointer_arguments();
    check_literals_and_conversions();
    check_packed_lowering();
    check_bounds_graph();
    check_guarded_bounds();
    TEST_SUMMARY("test_dxil_native_projection_expr");
}
