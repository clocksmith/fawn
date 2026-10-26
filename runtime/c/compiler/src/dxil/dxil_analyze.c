/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "dxil_source_native_internal.h"
#include "../../include/doe_compiler_pool_growth.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

bool dxil_source_native_fail(DxilSourceBridge *b, uint32_t offset,
    DxilStatus status, const char *message) {
    if (b->status == DXIL_OK) {
        b->status = status;
        if (b->source.diagnostic->status == DOE_COMPILER_OK)
            (void)source_error(&b->source, status == DXIL_OUTPUT_TOO_LARGE ?
                DOE_COMPILER_OUTPUT_TOO_SMALL : DOE_COMPILER_UNSUPPORTED, offset, message);
    }
    return false;
}
static const char *copy_name(DxilSourceBridge *b, const char *text, size_t length) {
    if (length >= b->names_capacity - b->names_count) {
        (void)dxil_source_native_fail(b, 0u, DXIL_OUTPUT_TOO_LARGE, "native DXIL name capacity exceeded");
        return "";
    }
    char *name = b->names + b->names_count;
    memcpy(name, text, length); name[length] = '\0';
    b->names_count += length + 1u;
    return name;
}
const char *dxil_source_native_text(DxilSourceBridge *b, const char *text) {
    return copy_name(b, text, strlen(text));
}
const char *dxil_source_native_name(DxilSourceBridge *b, NameSpan name) {
    if (name.offset > b->source.length || name.length > b->source.length - name.offset) {
        (void)dxil_source_native_fail(b, name.offset, DXIL_INVALID_IR, "invalid typed native DXIL name span");
        return "";
    }
    return copy_name(b, b->source.source + name.offset, name.length);
}
bool dxil_source_native_value(DxilSourceBridge *b, DxilSourceFunction *function,
    uint32_t expression, ExpressionValue *value) {
    LocalBinding bindings[DXIL_IR_MAX_LOCALS];
    for (uint32_t i = 0u; i < function->scope_count; ++i)
        bindings[i] = function->bindings[function->scope[i]];
    LocalScope scope = {.values = bindings, .count = function->scope_count};
    if (!sema_expression(&b->source, expression, &scope, true, false, value))
        return dxil_source_native_fail(b, b->compiler->expressions[expression].source_offset,
            DXIL_INVALID_IR, "canonical native DXIL value evaluation failed");
    return true;
}
bool dxil_source_native_constant_if_known(DxilSourceBridge *b, DxilSourceFunction *function,
    uint32_t expression, ConstantValue *out, bool *known) {
    ExpressionValue value;
    if (!dxil_source_native_value(b, function, expression, &value)) return false;
    *known = !value.runtime && value.known && !value.overridable;
    if (*known) *out = value.constant;
    return true;
}
bool dxil_source_native_constant(DxilSourceBridge *b, DxilSourceFunction *function,
    uint32_t expression, ConstantValue *out) {
    bool known = false;
    if (!dxil_source_native_constant_if_known(b, function, expression, out, &known)) return false;
    if (!known)
        return dxil_source_native_fail(b, b->compiler->expressions[expression].source_offset,
            DXIL_INVALID_IR, "native DXIL constant projection requires a canonical const value");
    return true;
}
static bool function_headers(DxilSourceBridge *b) {
    CompilerWorkspace *c = b->compiler;
    if (c->function_count > DXIL_IR_MAX_FUNCTIONS || c->entry_count > DXIL_IR_MAX_ENTRY_POINTS)
        return dxil_source_native_fail(b, 0u, DXIL_OUTPUT_TOO_LARGE, "native DXIL function/entry capacity exceeded");
    b->module->function_count = c->function_count;
    b->module->entry_point_count = c->entry_count;
    for (uint32_t i = 0u; i < c->function_count; ++i) {
        const FunctionRecord *f = &c->functions[i];
        DxilIrFunction *out = &b->module->functions[i];
        out->name = dxil_source_native_name(b, f->name);
        out->return_type = dxil_source_native_type(b, f->result_type);
        out->has_stage = (f->flags & FUNCTION_ENTRY) != 0u;
        out->stage = (f->flags & FUNCTION_VERTEX) != 0u ? DXIL_IR_STAGE_VERTEX :
            (f->flags & FUNCTION_FRAGMENT) != 0u ? DXIL_IR_STAGE_FRAGMENT : DXIL_IR_STAGE_COMPUTE;
        out->has_return_io = dxil_source_native_io(b, STAGE_ATTRIBUTE_RESULT, i, &out->return_io);
        const EntryRecord *entry = out->has_stage ? &c->entries[f->entry_index] : NULL;
        const bool compute = entry != NULL && !stage_graphics(f);
        const uint32_t first = stage_graphics(f) ? b->source.stages->entries[f->entry_index].formal_first : f->formal_start;
        out->param_count = compute ? entry->input_count : stage_graphics(f) ?
            b->source.stages->entries[f->entry_index].formal_count : f->formal_count;
        if (out->param_count > DXIL_IR_MAX_PARAMS)
            return dxil_source_native_fail(b, f->name.offset, DXIL_OUTPUT_TOO_LARGE, "native DXIL parameter capacity exceeded");
        if (entry != NULL) {
            memcpy(out->workgroup_size, entry->state.resolved.workgroup_size, sizeof(out->workgroup_size));
            b->module->entry_points[f->entry_index] = (DxilIrEntryPoint){.function = i, .stage = out->stage};
            memcpy(b->module->entry_points[f->entry_index].workgroup_size,
                out->workgroup_size, sizeof(out->workgroup_size));
        }
        for (uint32_t j = 0u; j < out->param_count; ++j) {
            DxilIrParam *parameter = &out->params[j];
            if (!compute) {
                const FormalRecord *formal = &c->formals[first + j];
                parameter->name = dxil_source_native_name(b, formal->name);
                parameter->ty = dxil_source_native_type(b, formal->type.resolved.type);
                parameter->has_io = dxil_source_native_io(b, STAGE_ATTRIBUTE_PARAMETER, first + j, &parameter->io);
            } else {
                const EntryInput *input = &c->entry_inputs[entry->input_first + j];
                parameter->name = dxil_source_native_name(b, input->name);
                parameter->has_io = true; parameter->ty = IR_U32;
                switch (input->kind) {
                    case INPUT_GLOBAL_INVOCATION: parameter->io.builtin = DXIL_IR_BUILTIN_GLOBAL_INVOCATION_ID; break;
                    case INPUT_LOCAL_INVOCATION: parameter->io.builtin = DXIL_IR_BUILTIN_LOCAL_INVOCATION_ID; break;
                    case INPUT_WORKGROUP: parameter->io.builtin = DXIL_IR_BUILTIN_WORKGROUP_ID; break;
                    case INPUT_NUM_WORKGROUPS: parameter->io.builtin = DXIL_IR_BUILTIN_NUM_WORKGROUPS; break;
                    case INPUT_LOCAL_INDEX: parameter->io.builtin = DXIL_IR_BUILTIN_LOCAL_INVOCATION_INDEX; break;
                    case INPUT_SUBGROUP_SIZE: parameter->io.builtin = DXIL_IR_BUILTIN_SUBGROUP_SIZE; break;
                    case INPUT_SUBGROUP_INVOCATION: parameter->io.builtin = DXIL_IR_BUILTIN_SUBGROUP_INVOCATION_ID; break;
                    case INPUT_NUM_SUBGROUPS: case INPUT_SUBGROUP_ID:
                        return dxil_source_native_fail(b, input->name.offset, DXIL_UNSUPPORTED_CONSTRUCT,
                            "compute builtin is outside retained native DXIL IR");
                }
                if (!input_scalar(input->kind)) {
                    parameter->ty = DXIL_IR_INVALID_ID;
                    for (uint32_t t = 0u; t < b->module->type_count; ++t) {
                        const DxilIrType *type = &b->module->types[t];
                        if (type->kind == DXIL_IR_TYPE_VECTOR && type->elem == IR_U32 && type->len == 3u) parameter->ty = t;
                    }
                    if (parameter->ty == DXIL_IR_INVALID_ID)
                        return dxil_source_native_fail(b, input->name.offset, DXIL_INVALID_IR, "missing canonical compute input vector type");
                }
            }
        }
    }
    return b->status == DXIL_OK;
}
static DxilDiagKind cause(SourceErrorCause cause, DoeCompilerStatus status) {
    switch (cause) {
        case SOURCE_CAUSE_UNKNOWN_IDENTIFIER: return DXIL_DIAG_UNKNOWN_IDENTIFIER;
        case SOURCE_CAUSE_UNKNOWN_TYPE: return DXIL_DIAG_UNKNOWN_TYPE;
        case SOURCE_CAUSE_DUPLICATE_SYMBOL: return DXIL_DIAG_DUPLICATE_SYMBOL;
        case SOURCE_CAUSE_TYPE_MISMATCH: return DXIL_DIAG_TYPE_MISMATCH;
        case SOURCE_CAUSE_INVALID_TYPE: return DXIL_DIAG_INVALID_TYPE;
        case SOURCE_CAUSE_INVALID_ATTRIBUTE: return DXIL_DIAG_INVALID_ATTRIBUTE;
        case SOURCE_CAUSE_UNSUPPORTED_BUILTIN: return DXIL_DIAG_UNSUPPORTED_BUILTIN;
        case SOURCE_CAUSE_NONE: break;
    }
    switch (status) {
        case DOE_COMPILER_PARSE_ERROR: return DXIL_DIAG_UNEXPECTED_TOKEN;
        case DOE_COMPILER_UNSUPPORTED: return DXIL_DIAG_UNSUPPORTED_CONSTRUCT;
        case DOE_COMPILER_OUTPUT_TOO_SMALL: return DXIL_DIAG_OUTPUT_TOO_LARGE;
        case DOE_COMPILER_OK: case DOE_COMPILER_ABI_MISMATCH: return DXIL_DIAG_INVALID_IR;
        case DOE_COMPILER_INPUT_ERROR: case DOE_COMPILER_SEMANTIC_ERROR: return DXIL_DIAG_INVALID_WGSL;
    }
    return DXIL_DIAG_INVALID_IR;
}
static void publish_error(DxilSourceBridge *b, DxilDiagnostic *out, DxilDiagKind *out_error) {
    const DoeCompilerDiagnostic *error = b->source.diagnostic;
    DxilDiagKind kind = cause(b->source.error_cause, error->status);
    if (b->status == DXIL_OUTPUT_TOO_LARGE) kind = DXIL_DIAG_OUTPUT_TOO_LARGE;
    if (b->status == DXIL_OUT_OF_MEMORY) kind = DXIL_DIAG_OUT_OF_MEMORY;
    if (b->status == DXIL_INVALID_IR) kind = DXIL_DIAG_INVALID_IR;
    const DxilDiagnosticStage stage = error->status == DOE_COMPILER_PARSE_ERROR ? DXIL_DIAG_STAGE_PARSER :
        b->status != DXIL_OK ? DXIL_DIAG_STAGE_IR_BUILDER : DXIL_DIAG_STAGE_SEMA;
    DxilDiagnostic location = {0};
    dxil_diagnostic_set(&location, stage, kind, b->source.source, b->source.length, error->offset);
    dxil_diagnostic_set_detail(out, stage, kind, error->message);
    if (out != NULL) {
        out->line = error->line; out->column = error->column;
        out->context_len = location.context_len;
        memcpy(out->context, location.context, sizeof(out->context));
    }
    if (out_error != NULL) *out_error = kind;
}
bool dxil_analyze(const char *source, size_t length,
    DxilIrModule *module, char *names, size_t name_capacity,
    DxilDiagnostic *diagnostic, DxilDiagKind *out_error) {
    const int saved_errno = errno;
    DoeCompilerDiagnostic error = {0};
    DxilSourceBridge b = {.source = {.source = source == NULL && length == 0u ? "" : source,
        .length = length, .diagnostic = &error}, .module = module,
        .names = names, .names_capacity = name_capacity};
    DoeCompilerSpecializationCapacities caps = {.target = {.stages = {.module = {.typed = {
        .source_bytes = 2u * 1024u * 1024u, .tokens = 4096u, .statements = 512u,
        .expressions = 2048u, .types = 256u, .members = 512u, .resources = 128u,
        .access_steps = 4096u, .constants = 2048u, .constant_edges = 4096u,
        .ir_instructions = 16384u, .ir_operands = 8192u, .target_declarations = 4096u,
        .traversal_items = 512u, .expression_depth = 64u, .control_depth = 32u, .type_syntax_depth = 64u},
        .entries = 64u, .entry_inputs = 256u, .entry_references = 8192u},
        .interface_attributes = 512u, .stage_interfaces = 512u}}, .overrides = 256u};
    bool success = false;
    dxil_diagnostic_clear(diagnostic);
    if (module == NULL || (names == NULL && name_capacity != 0u)) {
        (void)source_error(&b.source, DOE_COMPILER_INPUT_ERROR, 0u, "native DXIL output storage is invalid");
        b.status = DXIL_INVALID_IR;
        goto finish;
    }
    for (;;) {
        CompilerLimits limits;
        size_t stage_base, bytes, override_base;
        DoeCompilerWorkspaceRequirements requirements;
        if (!stage_layout(&caps.target.stages, &limits, &stage_base, &bytes) ||
            !specialization_layout(DOE_COMPILER_TARGET_SPIRV, &caps, &override_base, &requirements) ||
            requirements.size_bytes > 256u * 1024u * 1024u) {
            (void)dxil_source_native_fail(&b, 0u, DXIL_OUTPUT_TOO_LARGE, "native DXIL compiler workspace budget exceeded");
            break;
        }
        free(b.compiler); b.compiler = calloc(1u, requirements.size_bytes);
        if (b.compiler == NULL) {
            (void)dxil_source_native_fail(&b, 0u, DXIL_OUT_OF_MEMORY, "native DXIL compiler allocation failed");
            break;
        }
        compiler_job_bind(b.compiler, b.compiler, &limits);
        uint32_t pool = DOE_COMPILER_POOL_NONE;
        size_t minimum = 0u;
        b.source.exhausted_pool = &pool; b.source.minimum_pool_capacity = &minimum;
        stage_bind(&b.source, b.compiler, &caps.target.stages, stage_base);
        /* Native source translation has no pipeline-value input. Preserve
         * known declaration defaults through the existing override owner;
         * symbolic validation remains the public compiler's default mode. */
        SpecializationContext specialization = {.default_initializers = true};
        specialization_bind(&specialization, b.compiler, override_base, &caps);
        b.source.specialization = &specialization;
        NumericEnvironment environment;
        memset(&error, 0, sizeof(error)); b.status = DXIL_OK; b.names_count = 0u;
        dxil_ir_init(module);
        success = compiler_analyze(&b.source, b.compiler, &environment);
        if (success) success = dxil_source_native_types(&b) && dxil_source_native_globals(&b) && function_headers(&b);
        for (uint32_t i = 0u; success && i < module->function_count; ++i)
            success = dxil_source_native_function(&b, i);
        if (!numeric_environment_end(&b.source, &environment)) success = false;
        if (success || pool == DOE_COMPILER_POOL_NONE) break;
        const bool grown = pool == DOE_COMPILER_POOL_OVERRIDES ?
            doe_compiler_grow_pool_capacity(&caps.overrides, minimum) :
            doe_compiler_grow_stage_pool(&caps.target.stages, error.status, pool, minimum);
        if (!grown) { b.status = DXIL_OUTPUT_TOO_LARGE; break; }
    }
finish:
    if (!success) {
        publish_error(&b, diagnostic, out_error);
        if (module != NULL) dxil_ir_init(module);
    }
    free(b.compiler);
    errno = saved_errno;
    return success;
}
