/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "test_util.h"
#include "dxil_analyze.h"
#include "dxil_emit.h"
#include "dxil_hlsl.h"
#include "dxil_translate.h"
#if !defined(_WIN32)
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#include <errno.h>

static DxilIrModule module;
static char names[65536];
static bool analyze(const char *source) {
    DxilDiagnostic diagnostic;
    DxilDiagKind error = DXIL_DIAG_INVALID_IR;
    const bool ok = dxil_analyze(source, strlen(source), &module, names, sizeof(names), &diagnostic, &error);
    CHECK_MSG(ok, "%s", diagnostic.message);
    return ok;
}
static const DxilIrGlobal *global(const char *name) {
    for (uint32_t i = 0u; i < module.global_count; ++i)
        if (strcmp(module.globals[i].name, name) == 0) return &module.globals[i];
    CHECK(false); return NULL;
}
static void declarations(void) {
    if (!analyze("const offset=vec2i(-2,3)+vec2i(1,2); @id(7) override amount:u32=3u;"
        "override derived:u32=amount+1u;"
        "var<private> state:Later=Later(9u);"
        "@compute @workgroup_size(1) fn main(){let x=Later(amount); _=x.value;}"
        "struct Later{value:u32,}")) return;
    const DxilIrGlobal *offset = global("offset"), *amount = global("amount"), *state = global("state");
    if (offset != NULL) {
        CHECK(offset->has_initializer && offset->has_init_tree);
        const DxilIrConstant *tree = &module.constants[offset->init_tree];
        CHECK(tree->composite && tree->children.len == 2u);
        CHECK((uint32_t)module.constants[module.constant_children[tree->children.start]].int_value == UINT32_MAX);
        CHECK(module.constants[module.constant_children[tree->children.start + 1u]].int_value == 5u);
    }
    if (amount != NULL) {
        CHECK(amount->has_override_id && amount->override_id == 7u);
        CHECK(amount->has_initializer && amount->init_int == 3u);
    }
    const DxilIrGlobal *derived = global("derived");
    if (derived != NULL) CHECK(derived->has_initializer && derived->init_int == 4u);
    if (state != NULL) {
        CHECK(state->has_init_tree && module.constants[state->init_tree].composite);
        CHECK(module.constants[module.constant_children[module.constants[state->init_tree].children.start]].int_value == 9u);
    }
    static char hlsl[65536]; size_t written = 0u;
    CHECK_STATUS(dxil_hlsl_emit(&module, hlsl, sizeof(hlsl), &written), DXIL_OK);
    CHECK(written != 0u);
}
static void controls(void) {
    if (!analyze("@compute @workgroup_size(1) fn main(){var sum=0u;"
        "for(var i=0u;i<4u;i+=1u){switch i {case 0u,1u:{sum+=i;} default:{sum+=2u;}}}"
        "loop {let previous=sum; continuing {sum=previous+1u;break if sum>8u;}}}")) return;
    const DxilIrFunction *f = &module.functions[0];
    unsigned loops = 0u, switches = 0u;
    for (uint32_t i = 0u; i < f->stmt_count; ++i) {
        const DxilIrStmt *s = &f->stmts[i];
        if (s->kind == DXIL_IR_STMT_LOOP) {
            ++loops; CHECK(s->body < f->stmt_count);
            CHECK(s->has_continuing && s->continuing < f->stmt_count);
            if (s->loop_kind == DXIL_IR_LOOP_FOR) CHECK(s->has_init && s->has_cond);
        }
        if (s->kind == DXIL_IR_STMT_SWITCH) {
            ++switches; CHECK(s->cases.len == 2u);
            CHECK(f->switch_cases[s->cases.start].selectors.len == 2u);
            CHECK(f->switch_cases[s->cases.start + 1u].is_default);
            for (uint32_t j = 0u; j < s->cases.len; ++j)
                CHECK(f->switch_cases[s->cases.start + j].body < f->stmt_count);
        }
    }
    CHECK(loops == 2u && switches == 1u);
    static uint8_t output[65536]; size_t written = 0u;
    CHECK_STATUS(dxil_emit(&module, output, sizeof(output), &written, NULL), DXIL_OK);
    CHECK(written > 32u);
    uint8_t small[16], original[16];
    memset(small, 0xa5, sizeof(small)); memcpy(original, small, sizeof(small));
    written = 73u;
    CHECK_STATUS(dxil_emit(&module, small, sizeof(small), &written, NULL), DXIL_OUTPUT_TOO_LARGE);
    CHECK(written == 73u && memcmp(small, original, sizeof(small)) == 0);
}
static void graphics(void) {
    if (!analyze("struct Out{@builtin(position) position:vec4f,@location(0) uv:vec2f,}"
        "@vertex fn vertex_main(@builtin(vertex_index) index:u32)->Out{return Out(vec4f(f32(index),0,0,1),vec2f(0));}"
        "@fragment fn fragment_main(@location(0) uv:vec2f)->@location(0) vec4f{return vec4f(uv,0,1);}")) return;
    CHECK(module.entry_point_count == 2u);
    CHECK(module.functions[0].stage == DXIL_IR_STAGE_VERTEX && module.functions[0].param_count == 1u);
    CHECK(module.functions[0].params[0].io.builtin == DXIL_IR_BUILTIN_VERTEX_INDEX);
    CHECK(module.functions[1].stage == DXIL_IR_STAGE_FRAGMENT && module.functions[1].param_count == 1u);
    CHECK(module.functions[1].params[0].has_io && module.functions[1].params[0].io.has_location);
    CHECK(module.functions[1].has_return_io && module.functions[1].return_io.location == 0u);
    CHECK(module.structs[0].fields[0].io.builtin == DXIL_IR_BUILTIN_POSITION);
    CHECK(module.structs[0].fields[1].io.has_location);
}
static void assignment_types(void) {
    if (!analyze("struct S{m:mat2x2f,v:vec2u,} @compute @workgroup_size(1) fn main(){"
        "var s:S;s.m[0]=vec2f(1);s.v.x=2u;}")) return;
    const DxilIrFunction *f = &module.functions[0];
    unsigned assignments = 0u;
    for (uint32_t i = 0u; i < f->stmt_count; ++i) if (f->stmts[i].kind == DXIL_IR_STMT_ASSIGN) {
        const DxilIrExpr *lhs = &f->exprs[f->stmts[i].assign_lhs];
        CHECK(lhs->category == DXIL_IR_EXPR_REF);
        CHECK(lhs->ty < module.type_count);
        if (lhs->ty < module.type_count) {
            const DxilIrType *type = &module.types[lhs->ty];
            if (assignments == 0u) CHECK(type->kind == DXIL_IR_TYPE_VECTOR && type->len == 2u &&
                module.types[type->elem].scalar == DXIL_IR_SCALAR_F32);
            else CHECK(type->kind == DXIL_IR_TYPE_SCALAR && type->scalar == DXIL_IR_SCALAR_U32);
        }
        ++assignments;
    }
    CHECK(assignments == 2u);
}
static void constant_folding(void) {
    if (!analyze("@compute @workgroup_size(1) fn main(){let n=2u+3u;let m=~6u;"
        "const factor=7u;let k=factor*6u;let f=2.0f+3.0f;}")) return;
    const DxilIrFunction *function = &module.functions[0];
    const uint64_t values[] = {5u, UINT32_MAX - 6u, 7u, 42u};
    unsigned index = 0u;
    for (uint32_t i = 0u; i < function->stmt_count; ++i) {
        const DxilIrStmt *s = &function->stmts[i];
        if (s->kind != DXIL_IR_STMT_LOCAL_DECL || !s->has_initializer) continue;
        const DxilIrExpr *expression = &function->exprs[s->initializer];
        if (index < 4u) {
            CHECK(expression->kind == DXIL_IR_EXPR_INT_LIT);
            CHECK(expression->uint_value == values[index]);
        } else CHECK(expression->kind == DXIL_IR_EXPR_BINARY);
        ++index;
    }
    CHECK(index == 5u);
}
static void bounded_stack(void) {
#if !defined(_WIN32)
    const pid_t child = fork();
    CHECK(child >= 0);
    if (child < 0) return;
    if (child == 0) {
        const struct rlimit limit = {1024u * 1024u, 1024u * 1024u};
        if (setrlimit(RLIMIT_STACK, &limit) != 0) _exit(3);
        uint8_t output[4096];
        size_t written = 0u;
        DxilDiagnostic diagnostic;
        DxilTranslateStatus status;
        const char source[] = "@compute @workgroup_size(1) fn main(){let n=2u+3u;}";
        errno = ERANGE;
        const bool ok = dxil_translate_with_diagnostic(source, sizeof(source) - 1u,
            output, sizeof(output), &written, &diagnostic, &status);
        _exit(ok && written > 32u && errno == ERANGE ? 0 : 4);
    }
    int status = 0;
    CHECK(waitpid(child, &status, 0) == child);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
#endif
}
static void diagnostics(void) {
    static const struct {const char *source; DxilDiagKind kind;} cases[] = {
        {"@compute @workgroup_size(1) fn main(){let x=absent;}", DXIL_DIAG_UNKNOWN_IDENTIFIER},
        {"fn helper(x:Absent){}", DXIL_DIAG_UNKNOWN_TYPE},
        {"fn same(){} fn same(){}", DXIL_DIAG_DUPLICATE_SYMBOL},
        {"@compute @workgroup_size(1) fn main(){let x:u32=true;}", DXIL_DIAG_TYPE_MISMATCH},
        {"@id(4) override a=1u; @id(4) override b=2u;", DXIL_DIAG_INVALID_ATTRIBUTE},
        {"fn main( {", DXIL_DIAG_UNEXPECTED_TOKEN}
    };
    for (size_t i = 0u; i < sizeof(cases)/sizeof(cases[0]); ++i) {
        DxilDiagnostic diagnostic; DxilDiagKind error = DXIL_DIAG_INVALID_IR;
        CHECK(!dxil_analyze(cases[i].source, strlen(cases[i].source), &module,
            names, sizeof(names), &diagnostic, &error));
        CHECK_MSG(error == cases[i].kind, "unexpected category: %s", diagnostic.message);
        CHECK(diagnostic.has_kind && diagnostic.context_len != 0u);
        CHECK(module.function_count == 0u && module.global_count == 0u);
    }
    CHECK(analyze("@compute @workgroup_size(1) fn main(){}"));
}
int main(void) {
    errno = ERANGE;
    declarations(); controls(); graphics(); assignment_types(); constant_folding(); bounded_stack(); diagnostics();
    CHECK(errno == ERANGE);
    TEST_SUMMARY("native typed projection");
}
