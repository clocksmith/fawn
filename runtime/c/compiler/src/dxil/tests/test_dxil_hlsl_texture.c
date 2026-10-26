/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "test_util.h"
#include "dxil_hlsl_texture.h"

#include <stdlib.h>

/* Caller-built IR exercises the compatibility consumer, not the WGSL shortcut.
 * String assertions are serialization controls, never DXC/GPU qualification. */
typedef struct TextureFixture {
    DxilIrModule module;
    DxilIrFunction *function;
    DxilIrTypeId void_type, f32, u32, i32, f2, f3, f4, i2, u2, u3;
    DxilIrExprId image, depth, cube, volume, array, line, storage, readonly;
    DxilIrExprId sampler, comparison, uv, uv3, coord, coord1, lod, level, value, offset;
    DxilIrExprId component, call;
    uint32_t base_args;
} TextureFixture;

static DxilIrTypeId type_add(TextureFixture *fixture, DxilIrType type) {
    DxilIrTypeId id = DXIL_IR_INVALID_ID;
    DxilStatus status = dxil_ir_intern_type(&fixture->module, &type, &id);
    CHECK_STATUS(status, DXIL_OK);
    return id;
}

static DxilIrExprId expression(TextureFixture *fixture, DxilIrExpr node) {
    DxilIrExprId id = DXIL_IR_INVALID_ID;
    DxilStatus status = dxil_ir_append_expr(fixture->function, &node, &id);
    CHECK_STATUS(status, DXIL_OK);
    return id;
}

static DxilIrExprId parameter(TextureFixture *fixture, const char *name, DxilIrTypeId type) {
    DxilIrFunction *function = fixture->function;
    uint32_t index = function->param_count++;
    function->params[index] = (DxilIrParam){.name = name, .ty = type};
    return expression(fixture, (DxilIrExpr){.kind = DXIL_IR_EXPR_PARAM_REF, .ty = type, .index = index});
}

static DxilIrExprId resource(TextureFixture *fixture, const char *name,
    DxilIrTypeKind kind, DxilIrAccessMode access) {
    DxilIrTypeId type = type_add(fixture, (DxilIrType){.kind = kind, .access = access,
        .format = DXIL_IR_TEXFMT_RGBA8UNORM});
    DxilIrGlobal global = {.name = name, .ty = type, .class = DXIL_IR_GLOBAL_VAR,
        .has_addr_space = true, .addr_space = DXIL_IR_ADDR_HANDLE, .has_binding = true,
        .binding = {2u, fixture->module.global_count}};
    DxilIrGlobalId id = DXIL_IR_INVALID_ID;
    DxilStatus status = dxil_ir_add_global(&fixture->module, &global, &id);
    CHECK_STATUS(status, DXIL_OK);
    return expression(fixture, (DxilIrExpr){.kind = DXIL_IR_EXPR_GLOBAL_REF, .ty = type, .index = id});
}

static TextureFixture *fixture_new(void) {
    TextureFixture *fixture = calloc(1u, sizeof(*fixture));
    CHECK(fixture != NULL);
    if (!fixture) exit(2);
    dxil_ir_init(&fixture->module);
    fixture->void_type = type_add(fixture, (DxilIrType){.kind = DXIL_IR_TYPE_SCALAR, .scalar = DXIL_IR_SCALAR_VOID});
    fixture->f32 = type_add(fixture, (DxilIrType){.kind = DXIL_IR_TYPE_SCALAR, .scalar = DXIL_IR_SCALAR_F32});
    fixture->u32 = type_add(fixture, (DxilIrType){.kind = DXIL_IR_TYPE_SCALAR, .scalar = DXIL_IR_SCALAR_U32});
    fixture->i32 = type_add(fixture, (DxilIrType){.kind = DXIL_IR_TYPE_SCALAR, .scalar = DXIL_IR_SCALAR_I32});
    fixture->f2 = type_add(fixture, (DxilIrType){.kind = DXIL_IR_TYPE_VECTOR, .elem = fixture->f32, .len = 2u});
    fixture->f3 = type_add(fixture, (DxilIrType){.kind = DXIL_IR_TYPE_VECTOR, .elem = fixture->f32, .len = 3u});
    fixture->f4 = type_add(fixture, (DxilIrType){.kind = DXIL_IR_TYPE_VECTOR, .elem = fixture->f32, .len = 4u});
    fixture->i2 = type_add(fixture, (DxilIrType){.kind = DXIL_IR_TYPE_VECTOR, .elem = fixture->i32, .len = 2u});
    fixture->u2 = type_add(fixture, (DxilIrType){.kind = DXIL_IR_TYPE_VECTOR, .elem = fixture->u32, .len = 2u});
    fixture->u3 = type_add(fixture, (DxilIrType){.kind = DXIL_IR_TYPE_VECTOR, .elem = fixture->u32, .len = 3u});
    DxilStatus status = dxil_ir_add_function(&fixture->module, "exercise", fixture->f4, &fixture->function);
    CHECK_STATUS(status, DXIL_OK);
    fixture->image = resource(fixture, "image", DXIL_IR_TYPE_TEXTURE_2D, DXIL_IR_ACCESS_READ);
    fixture->depth = resource(fixture, "depth_image", DXIL_IR_TYPE_TEXTURE_DEPTH_2D, DXIL_IR_ACCESS_READ);
    fixture->cube = resource(fixture, "cube_image", DXIL_IR_TYPE_TEXTURE_CUBE, DXIL_IR_ACCESS_READ);
    fixture->volume = resource(fixture, "volume_image", DXIL_IR_TYPE_TEXTURE_3D, DXIL_IR_ACCESS_READ);
    fixture->array = resource(fixture, "array_image", DXIL_IR_TYPE_TEXTURE_2D_ARRAY, DXIL_IR_ACCESS_READ);
    fixture->line = resource(fixture, "line_image", DXIL_IR_TYPE_TEXTURE_1D, DXIL_IR_ACCESS_READ);
    fixture->storage = resource(fixture, "storage_image", DXIL_IR_TYPE_STORAGE_TEXTURE_2D, DXIL_IR_ACCESS_READ_WRITE);
    fixture->readonly = resource(fixture, "readonly_image", DXIL_IR_TYPE_STORAGE_TEXTURE_2D, DXIL_IR_ACCESS_READ);
    fixture->sampler = resource(fixture, "linear_sampler", DXIL_IR_TYPE_SAMPLER, DXIL_IR_ACCESS_READ);
    fixture->comparison = resource(fixture, "compare_sampler", DXIL_IR_TYPE_SAMPLER_COMPARISON, DXIL_IR_ACCESS_READ);
    fixture->uv = parameter(fixture, "uv", fixture->f2);
    fixture->uv3 = parameter(fixture, "uv3", fixture->f3);
    fixture->coord = parameter(fixture, "coord", fixture->i2);
    fixture->coord1 = parameter(fixture, "coord1", fixture->i32);
    fixture->lod = parameter(fixture, "lod", fixture->f32);
    fixture->level = parameter(fixture, "level", fixture->u32);
    fixture->value = parameter(fixture, "value", fixture->f4);
    fixture->component = expression(fixture, (DxilIrExpr){.kind = DXIL_IR_EXPR_INT_LIT,
        .ty = fixture->i32, .uint_value = 0u});
    DxilIrExprId one = expression(fixture, (DxilIrExpr){.kind = DXIL_IR_EXPR_INT_LIT,
        .ty = fixture->i32, .uint_value = 1u});
    DxilIrExprId two = expression(fixture, (DxilIrExpr){.kind = DXIL_IR_EXPR_INT_LIT,
        .ty = fixture->i32, .uint_value = 2u});
    status = dxil_ir_append_expr_arg(fixture->function, one);
    CHECK_STATUS(status, DXIL_OK);
    status = dxil_ir_append_expr_arg(fixture->function, two);
    CHECK_STATUS(status, DXIL_OK);
    fixture->offset = expression(fixture, (DxilIrExpr){.kind = DXIL_IR_EXPR_CONSTRUCT,
        .ty = fixture->i2, .construct_ty = fixture->i2, .args = {0u, 2u}});
    fixture->base_args = fixture->function->expr_arg_count;
    fixture->call = expression(fixture, (DxilIrExpr){.kind = DXIL_IR_EXPR_CALL,
        .call_kind = DXIL_IR_CALL_BUILTIN, .ty = fixture->f4, .name = "textureLoad"});
    fixture->function->stmts[0] = (DxilIrStmt){.kind = DXIL_IR_STMT_RETURN,
        .has_return_value = true, .return_value = fixture->call};
    fixture->function->stmts[1] = (DxilIrStmt){.kind = DXIL_IR_STMT_BLOCK, .range = {0u, 1u}};
    fixture->function->stmt_count = 2u;
    fixture->function->stmt_children[0] = 0u;
    fixture->function->stmt_child_count = 1u;
    fixture->function->root_stmt = 1u;
    return fixture;
}

static void select_call(TextureFixture *fixture, const char *name, DxilIrTypeId result,
    const DxilIrExprId *operands, uint32_t count) {
    DxilIrFunction *function = fixture->function;
    function->return_type = result;
    function->expr_arg_count = fixture->base_args;
    for (uint32_t i = 0u; i < count; ++i) {
        DxilStatus status = dxil_ir_append_expr_arg(function, operands[i]);
        CHECK_STATUS(status, DXIL_OK);
    }
    function->exprs[fixture->call].name = name;
    function->exprs[fixture->call].ty = result;
    function->exprs[fixture->call].args = (DxilIrRange){fixture->base_args, count};
    function->stmts[0] = result == fixture->void_type ?
        (DxilIrStmt){.kind = DXIL_IR_STMT_EXPR, .expr = fixture->call} :
        (DxilIrStmt){.kind = DXIL_IR_STMT_RETURN, .has_return_value = true, .return_value = fixture->call};
}

static void check_text(TextureFixture *fixture, const char *expected) {
    char *output = malloc(131072u);
    CHECK(output != NULL);
    if (!output) exit(2);
    size_t written = 0u;
    DxilStatus status = dxil_hlsl_emit(&fixture->module, output, 131072u, &written);
    CHECK_STATUS(status, DXIL_OK);
    CHECK(written > 0u && written < 131072u);
    if (status == DXIL_OK) CHECK_MSG(strstr(output, expected) != NULL, "missing: %s", expected);
    free(output);
}

static void check_operations(void) {
    TextureFixture *f = fixture_new();
    select_call(f, "textureLoad", f->f4, (DxilIrExprId[]){f->image, f->coord, f->level}, 3u);
    check_text(f, "return doe_textureLoad_image(coord, level);");
    check_text(f, "image.Load(int3(coord, level));");
    check_text(f, "level < 0 || uint(level) >= doe_textureNumLevels_image()");
    check_text(f, "any(coord < int2(0, 0)) || any(uint2(coord) >= doe_textureDimensions_image(uint(level)))");
    select_call(f, "textureSample", f->f4, (DxilIrExprId[]){f->image, f->sampler, f->uv}, 3u);
    check_text(f, "return image.Sample(linear_sampler, uv);");
    select_call(f, "textureSampleLevel", f->f4, (DxilIrExprId[]){f->image, f->sampler, f->uv, f->lod}, 4u);
    check_text(f, "return image.SampleLevel(linear_sampler, uv, lod);");
    select_call(f, "textureSampleCompare", f->f32, (DxilIrExprId[]){f->depth, f->comparison, f->uv, f->lod}, 4u);
    check_text(f, "return depth_image.SampleCmp(compare_sampler, uv, lod);");
    check_text(f, "SamplerComparisonState compare_sampler : register(s9, space2);");
    select_call(f, "textureSampleCompareLevel", f->f32, (DxilIrExprId[]){f->depth, f->comparison, f->uv, f->lod}, 4u);
    check_text(f, "return depth_image.SampleCmpLevelZero(compare_sampler, uv, lod);");
    select_call(f, "textureGather", f->f4, (DxilIrExprId[]){f->component, f->image, f->sampler, f->uv}, 4u);
    const char *gather[] = {"return image.GatherRed(", "return image.GatherGreen(",
        "return image.GatherBlue(", "return image.GatherAlpha("};
    for (uint64_t i = 0u; i < 4u; ++i) {
        f->function->exprs[f->component].uint_value = i;
        check_text(f, gather[i]);
    }
    select_call(f, "textureGatherCompare", f->f4, (DxilIrExprId[]){f->depth, f->comparison, f->uv, f->lod}, 4u);
    check_text(f, "return depth_image.GatherCmp(compare_sampler, uv, lod);");
    select_call(f, "textureSampleGrad", f->f4, (DxilIrExprId[]){f->image, f->sampler, f->uv, f->uv, f->uv}, 5u);
    check_text(f, "return image.SampleGrad(linear_sampler, uv, uv, uv);");
    select_call(f, "textureSampleOffset", f->f4, (DxilIrExprId[]){f->image, f->sampler, f->uv, f->offset}, 4u);
    check_text(f, "return image.Sample(linear_sampler, uv, int2(1, 2));");
    select_call(f, "textureSampleLevelOffset", f->f4, (DxilIrExprId[]){f->image, f->sampler, f->uv, f->lod, f->offset}, 5u);
    check_text(f, "return image.SampleLevel(linear_sampler, uv, lod, int2(1, 2));");
    select_call(f, "textureStore", f->void_type, (DxilIrExprId[]){f->storage, f->coord, f->value}, 3u);
    check_text(f, "doe_textureStore_storage_image(coord, value);");
    check_text(f, "storage_image[uint2(coord)] = value;");
    select_call(f, "textureDimensions", f->u2, (DxilIrExprId[]){f->image, f->level}, 2u);
    check_text(f, "return doe_textureDimensions_image(level);");
    select_call(f, "textureNumLevels", f->u32, (DxilIrExprId[]){f->image}, 1u);
    check_text(f, "return doe_textureNumLevels_image();");
    select_call(f, "textureNumLayers", f->u32, (DxilIrExprId[]){f->array}, 1u);
    check_text(f, "return doe_textureNumLayers_array_image();");
    check_text(f, "array_image.GetDimensions(0u, w, h, d, lvls);\n    return d;");
    free(f);
}

static void check_shapes(void) {
    TextureFixture *f = fixture_new();
    select_call(f, "textureLoad", f->f4, (DxilIrExprId[]){f->line, f->coord1, f->level}, 3u);
    check_text(f, "return doe_textureLoad_line_image(coord1, level);");
    check_text(f, "line_image.Load(int2(coord, level));");
    select_call(f, "textureLoad", f->f4, (DxilIrExprId[]){f->storage, f->coord}, 2u);
    check_text(f, "return doe_textureLoad_storage_image(coord);");
    check_text(f, "return storage_image[uint2(coord)];");
    select_call(f, "textureLoad", f->f4, (DxilIrExprId[]){f->readonly, f->coord}, 2u);
    check_text(f, "return doe_textureLoad_readonly_image(coord);");
    check_text(f, "return readonly_image.Load(int3(coord, 0));");
    select_call(f, "textureDimensions", f->u32, (DxilIrExprId[]){f->line, f->level}, 2u);
    check_text(f, "line_image.GetDimensions(level, w, lvls);");
    select_call(f, "textureDimensions", f->u3, (DxilIrExprId[]){f->volume, f->level}, 2u);
    check_text(f, "volume_image.GetDimensions(level, w, h, d, lvls);");
    select_call(f, "textureDimensions", f->u2, (DxilIrExprId[]){f->storage}, 1u);
    check_text(f, "return doe_textureDimensions_storage_image();");
    select_call(f, "textureSample", f->f4, (DxilIrExprId[]){f->cube, f->sampler, f->uv3}, 3u);
    check_text(f, "return cube_image.Sample(linear_sampler, uv3);");
    select_call(f, "textureSampleLevel", f->f4, (DxilIrExprId[]){f->volume, f->sampler, f->uv3, f->lod}, 4u);
    check_text(f, "return volume_image.SampleLevel(linear_sampler, uv3, lod);");
    /* Formal resource loads follow the same typed writer, without naming a
     * nonexistent global helper. Bound resources use guarded helper calls. */
    DxilIrExprId texture_parameter = parameter(f, "argument_image", f->function->exprs[f->image].ty);
    select_call(f, "textureLoad", f->f4, (DxilIrExprId[]){texture_parameter, f->coord, f->level}, 3u);
    check_text(f, "return argument_image.Load(int3(coord, level));");
    free(f);
}

static void check_operand_once(void) {
    TextureFixture *f = fixture_new();
    DxilIrFunction *producer = NULL;
    DxilStatus status = dxil_ir_add_function(&f->module, "next_coord", f->i2, &producer);
    CHECK_STATUS(status, DXIL_OK);
    /* A user call is not replay-safe, irrespective of its present body. */
    producer->exprs[0] = (DxilIrExpr){.kind = DXIL_IR_EXPR_INT_LIT, .ty = f->i32};
    producer->exprs[1] = (DxilIrExpr){.kind = DXIL_IR_EXPR_CONSTRUCT,
        .ty = f->i2, .construct_ty = f->i2, .args = {0u, 2u}};
    producer->expr_count = 2u;
    producer->expr_args[0] = 0u;
    producer->expr_args[1] = 0u;
    producer->expr_arg_count = 2u;
    producer->stmts[0] = (DxilIrStmt){.kind = DXIL_IR_STMT_RETURN,
        .has_return_value = true, .return_value = 1u};
    producer->stmts[1] = (DxilIrStmt){.kind = DXIL_IR_STMT_BLOCK, .range = {0u, 1u}};
    producer->stmt_count = 2u;
    producer->stmt_children[0] = 0u;
    producer->stmt_child_count = 1u;
    producer->root_stmt = 1u;
    /* HLSL definitions precede callers; retain declaration order independently
     * from the expression argument order under test. */
    DxilIrFunction *temporary = malloc(sizeof(*temporary));
    CHECK(temporary != NULL);
    if (!temporary) exit(2);
    *temporary = f->module.functions[0];
    f->module.functions[0] = f->module.functions[1];
    f->module.functions[1] = *temporary;
    free(temporary);
    f->function = &f->module.functions[1];
    DxilIrExprId coord_call = expression(f, (DxilIrExpr){.kind = DXIL_IR_EXPR_CALL,
        .call_kind = DXIL_IR_CALL_USER, .ty = f->i2, .name = "next_coord"});
    select_call(f, "textureLoad", f->f4, (DxilIrExprId[]){f->image, coord_call, f->level}, 3u);
    check_text(f, "return doe_textureLoad_image(next_coord(), level);");
    char *output = malloc(131072u);
    CHECK(output != NULL);
    if (!output) exit(2);
    size_t written = 0u;
    status = dxil_hlsl_emit(&f->module, output, 131072u, &written);
    CHECK_STATUS(status, DXIL_OK);
    if (status == DXIL_OK) {
        const char *call = strstr(output, "doe_textureLoad_image(next_coord(), level)");
        CHECK(call != NULL);
        const char *definition = strstr(output, "int2 next_coord()");
        CHECK(definition != NULL);
        size_t count = 0u;
        for (const char *at = output; (at = strstr(at, "next_coord()")) != NULL; ++at) ++count;
        CHECK(count == 2u); /* Exactly one call and one definition. */
    }
    select_call(f, "textureStore", f->void_type,
        (DxilIrExprId[]){f->storage, coord_call, f->value}, 3u);
    check_text(f, "doe_textureStore_storage_image(next_coord(), value);");
    free(output);
    free(f);
}

static void check_rejection(TextureFixture *f, DxilStatus expected) {
    char output[32768];
    memset(output, 0x4b, sizeof(output));
    size_t written = 123u;
    DxilStatus status = dxil_hlsl_emit(&f->module, output, sizeof(output), &written);
    CHECK_STATUS(status, expected);
    CHECK(written == 123u);
}

static void check_failures(void) {
    TextureFixture *f = fixture_new();
    select_call(f, "textureSample", f->f4, (DxilIrExprId[]){f->image, f->sampler}, 2u);
    check_rejection(f, DXIL_INVALID_IR);
    select_call(f, "textureSample", f->f4, (DxilIrExprId[]){f->image, f->comparison, f->uv}, 3u);
    check_rejection(f, DXIL_INVALID_IR);
    select_call(f, "textureSample", f->f4, (DxilIrExprId[]){f->image, f->sampler, f->uv3}, 3u);
    check_rejection(f, DXIL_INVALID_IR);
    select_call(f, "textureGather", f->f4, (DxilIrExprId[]){f->component, f->image, f->sampler, f->uv}, 4u);
    f->function->exprs[f->component].uint_value = 4u;
    check_rejection(f, DXIL_INVALID_IR);
    select_call(f, "textureStore", f->void_type, (DxilIrExprId[]){f->readonly, f->coord, f->value}, 3u);
    check_rejection(f, DXIL_INVALID_IR);
    select_call(f, "textureNumLayers", f->u32, (DxilIrExprId[]){f->image}, 1u);
    check_rejection(f, DXIL_UNSUPPORTED_BUILTIN);
    select_call(f, "textureLoad", f->f4, (DxilIrExprId[]){f->volume, f->coord, f->level}, 3u);
    check_rejection(f, DXIL_UNSUPPORTED_BUILTIN);
    select_call(f, "textureSample", f->f4, (DxilIrExprId[]){f->array, f->sampler, f->uv3}, 3u);
    check_rejection(f, DXIL_UNSUPPORTED_BUILTIN);
    select_call(f, "textureDimensions", f->u2, (DxilIrExprId[]){f->image}, 1u);
    check_rejection(f, DXIL_INVALID_IR);
    select_call(f, "textureLoad", f->f4, (DxilIrExprId[]){f->image, f->coord, f->level}, 3u);
    f->function->exprs[f->call].args.start = UINT32_MAX;
    check_rejection(f, DXIL_INVALID_IR);
    select_call(f, "textureLoad", f->f4, (DxilIrExprId[]){f->image, f->coord, f->level}, 3u);
    f->function->expr_args[f->base_args] = UINT32_MAX;
    check_rejection(f, DXIL_INVALID_IR);
    select_call(f, "textureLoad", f->f4, (DxilIrExprId[]){f->image, f->coord, f->level}, 3u);
    char output[40];
    memset(output, 0x5b, sizeof(output));
    size_t written = 456u;
    DxilStatus status = dxil_hlsl_emit(&f->module, output + 4u, 16u, &written);
    CHECK_STATUS(status, DXIL_OUTPUT_TOO_LARGE);
    CHECK(written == 456u);
    for (size_t i = 0u; i < sizeof(output); ++i) CHECK(output[i] == 0x5b);
    DxilHlslEmitter emitter = {.module = &f->module, .out = output, .out_len = sizeof(output)};
    bool handled = true;
    status = dxil_hlsl_emit_texture_builtin(&emitter, f->function, f->f4, "notATextureBuiltin",
        (DxilIrRange){UINT32_MAX, UINT32_MAX}, &handled);
    CHECK_STATUS(status, DXIL_OK);
    CHECK(!handled && emitter.pos == 0u);
    unsigned char guarded[140];
    for (size_t capacity = 0u; capacity <= 128u; capacity += 8u) {
        memset(guarded, 0x6c, sizeof(guarded));
        emitter = (DxilHlslEmitter){.module = &f->module,
            .out = (char *)guarded + 4u, .out_len = capacity};
        status = dxil_hlsl_emit_texture_helpers(&emitter);
        CHECK_STATUS(status, DXIL_OUTPUT_TOO_LARGE);
        CHECK(emitter.pos <= capacity);
        for (size_t i = 0u; i < 4u; ++i) CHECK(guarded[i] == 0x6cu);
        for (size_t i = 4u + capacity; i < sizeof(guarded); ++i) CHECK(guarded[i] == 0x6cu);
    }
    free(f);
}

int main(void) {
    check_operations();
    check_shapes();
    check_operand_once();
    check_failures();
    TEST_SUMMARY("dxil_hlsl_texture");
}
