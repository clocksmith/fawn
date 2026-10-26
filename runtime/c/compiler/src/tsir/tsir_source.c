/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "tsir_source_internal.h"
#include "tsir_sha256.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

bool tsir_source_fail(TsirSourceBridge *b, uint32_t offset, const char *message) {
    if (b->status == TSIR_OK) {
        b->status = TSIR_ERR_UNSUPPORTED;
        (void)source_error(&b->source, DOE_COMPILER_UNSUPPORTED, offset, message);
    }
    return false;
}
const char *tsir_source_name(TsirSourceBridge *b, NameSpan name) {
    if (name.offset > b->source.length || name.length > b->source.length - name.offset ||
        name.length + 1u > b->names_cap - b->names_count) {
        (void)tsir_source_fail(b, name.offset, "TSIR source name storage exhausted");
        return "";
    }
    char *result = b->names + b->names_count;
    memcpy(result, b->source.source + name.offset, name.length);
    result[name.length] = '\0';
    b->names_count += name.length + 1u;
    return result;
}
static TsirIrScalar scalar(TypeId id) {
    switch (id) {
        case IR_F32: return TSIR_IR_F32;
        case IR_F16: return TSIR_IR_F16;
        case IR_I32: return TSIR_IR_I32;
        case IR_U32: return TSIR_IR_U32;
        case IR_ABSTRACT_INT: return TSIR_IR_ABSTRACT_INT;
        case IR_ABSTRACT_FLOAT: return TSIR_IR_ABSTRACT_FLOAT;
        case IR_BOOL: return TSIR_IR_BOOL;
        case IR_VOID: case IR_PRIMITIVE_COUNT: return TSIR_IR_VOID;
    }
    return TSIR_IR_VOID;
}
/* Translate validated image properties to the retained TSIR rejection tag;
 * this preserves diagnostic identity without pretending a texture is a buffer. */
static const char *opaque_tag(const TypeRecord *type) {
    const ImageType image = type->state.image;
    if (type->kind == TYPE_SAMPLER)
        return (image.flags & DOE_COMPILER_HANDLE_COMPARISON) != 0u ? "sampler_comparison" : "sampler";
    if (type->kind != TYPE_TEXTURE || (image.flags & DOE_COMPILER_HANDLE_EXTERNAL) != 0u) return NULL;
    if (image.format != 0u)
        return image.dimension == DOE_COMPILER_VIEW_2D ? "storage_texture_2d" : NULL;
    if ((image.flags & DOE_COMPILER_HANDLE_DEPTH) != 0u) {
        if ((image.flags & DOE_COMPILER_HANDLE_MULTISAMPLED) != 0u) return NULL;
        if (image.dimension == DOE_COMPILER_VIEW_2D) return "texture_depth_2d";
        if (image.dimension == DOE_COMPILER_VIEW_CUBE) return "texture_depth_cube";
        return NULL;
    }
    if ((image.flags & DOE_COMPILER_HANDLE_MULTISAMPLED) != 0u)
        return "texture_multisampled_2d";
    switch (image.dimension) {
        case DOE_COMPILER_VIEW_1D: return "texture_1d";
        case DOE_COMPILER_VIEW_2D: return "texture_2d";
        case DOE_COMPILER_VIEW_2D_ARRAY: return "texture_2d_array";
        case DOE_COMPILER_VIEW_CUBE: return "texture_cube";
        case DOE_COMPILER_VIEW_3D: return "texture_3d";
        default: return NULL;
    }
}
static bool types(TsirSourceBridge *b) {
    CompilerWorkspace *c = b->compiler;
    b->module.type_count = (size_t)c->type_count + IR_PRIMITIVE_COUNT;
    b->types = calloc(b->module.type_count, sizeof(*b->types));
    b->structs = calloc(c->type_count + 1u, sizeof(*b->structs));
    b->fields = calloc(c->member_count + 1u, sizeof(*b->fields));
    if (!b->types || !b->structs || !b->fields) return false;
    b->module.types = b->types;
    b->module.structs = b->structs;
    for (uint32_t i = 0u; i < IR_PRIMITIVE_COUNT; ++i)
        b->types[i] = (TsirIrType){.kind = TSIR_IR_TYPE_SCALAR, .scalar = scalar(i)};
    for (uint32_t i = 0u; i < c->type_count; ++i) {
        const TypeRecord *t = &c->types[i];
        TsirIrType *o = &b->types[i + IR_PRIMITIVE_COUNT];
        o->elem = t->element;
        switch (t->kind) {
            case TYPE_VECTOR: o->kind = TSIR_IR_TYPE_VECTOR; o->len = t->count; break;
            case TYPE_MATRIX:
                o->kind = TSIR_IR_TYPE_MATRIX;
                o->columns = t->count;
                if (t->element < IR_PRIMITIVE_COUNT ||
                    t->element - IR_PRIMITIVE_COUNT >= c->type_count)
                    return tsir_source_fail(b, t->name.offset, "invalid typed matrix column");
                o->rows = c->types[t->element - IR_PRIMITIVE_COUNT].count;
                o->elem = c->types[t->element - IR_PRIMITIVE_COUNT].element;
                break;
            case TYPE_ARRAY: case TYPE_RUNTIME_ARRAY:
                o->kind = TSIR_IR_TYPE_ARRAY;
                o->has_len = t->kind == TYPE_ARRAY;
                o->array_len = t->count;
                break;
            case TYPE_STRUCT: {
                o->kind = TSIR_IR_TYPE_STRUCT;
                o->struct_id = (uint32_t)b->module.struct_count++;
                TsirIrStructDef *def = &b->structs[o->struct_id];
                def->name = tsir_source_name(b, t->name);
                def->fields = b->fields + t->element;
                def->field_count = t->count;
                for (uint32_t f = 0u; f < t->count; ++f) {
                    const MemberRecord *m = &c->members[t->element + f];
                    b->fields[t->element + f] = (TsirIrStructField){tsir_source_name(b, m->name), m->type};
                }
                break;
            }
            case TYPE_ATOMIC: o->kind = TSIR_IR_TYPE_ATOMIC; break;
            case TYPE_POINTER:
                o->kind = TSIR_IR_TYPE_REF;
                o->ref_access = t->count == POINTER_UNIFORM || t->count == POINTER_STORAGE_READ ?
                    TSIR_IR_ACCESS_READ : TSIR_IR_ACCESS_READ_WRITE;
                switch (t->count) {
                    case POINTER_FUNCTION: o->ref_addr_space = TSIR_IR_SPACE_FUNCTION; break;
                    case POINTER_PRIVATE: o->ref_addr_space = TSIR_IR_SPACE_PRIVATE; break;
                    case POINTER_WORKGROUP: o->ref_addr_space = TSIR_IR_SPACE_WORKGROUP; break;
                    case POINTER_UNIFORM: o->ref_addr_space = TSIR_IR_SPACE_UNIFORM; break;
                    case POINTER_STORAGE_READ: case POINTER_STORAGE_WRITE:
                        o->ref_addr_space = TSIR_IR_SPACE_STORAGE; break;
                    default: return tsir_source_fail(b, 0u, "TSIR pointer address space is not represented");
                }
                break;
            default:
                o->kind = TSIR_IR_TYPE_OTHER;
                o->unlowerable_tag = opaque_tag(t);
                break;
        }
    }
    return b->status == TSIR_OK;
}
static bool globals(TsirSourceBridge *b) {
    CompilerWorkspace *c = b->compiler;
    const size_t capacity = (size_t)c->resource_count + c->workgroup_count + c->constant_decl_count;
    b->globals = calloc(capacity + 1u, sizeof(*b->globals));
    if (!b->globals) return false;
    b->module.globals = b->globals;
    b->module.global_count = c->resource_count;
    for (uint32_t i = 0u; i < c->resource_count; ++i) {
        const ResourceRecord *r = &c->resources[i];
        const bool handle = type_handle(c, r->type);
        b->globals[i] = (TsirIrGlobal){.name = tsir_source_name(b, r->name),
            .ty = r->type, .class = TSIR_IR_GLOBAL_VAR, .has_addr_space = true,
            .addr_space = handle ? TSIR_IR_SPACE_HANDLE : r->kind == DOE_COMPILER_UNIFORM_BUFFER ? TSIR_IR_SPACE_UNIFORM : TSIR_IR_SPACE_STORAGE,
            .has_access = !handle && r->kind != DOE_COMPILER_UNIFORM_BUFFER,
            .access = r->access == DOE_COMPILER_READ_WRITE ? TSIR_IR_ACCESS_READ_WRITE : TSIR_IR_ACCESS_READ,
            .has_binding = true, .group = r->group, .binding = r->binding};
    }
    for (uint32_t i = 0u; i < c->workgroup_count; ++i) {
        const WorkgroupRoot *root = &c->workgroups[i];
        if (root->state.resolved.address_space != MODULE_WORKGROUP &&
            root->state.resolved.address_space != MODULE_PRIVATE)
            return tsir_source_fail(b, root->name.offset, "TSIR has no immediate address-space representation");
        b->globals[b->module.global_count++] = (TsirIrGlobal){
            .name = tsir_source_name(b, root->name), .ty = root->type,
            .class = TSIR_IR_GLOBAL_VAR, .has_addr_space = true,
            .addr_space = root->state.resolved.address_space == MODULE_PRIVATE ?
                TSIR_IR_SPACE_PRIVATE : TSIR_IR_SPACE_WORKGROUP};
    }
    for (uint32_t i = 0u; i < c->constant_decl_count; ++i) {
        const ConstantSyntax *declaration = &c->constant_decls[i];
        /* Type aliases resolve to canonical types and are not IR globals. */
        if (declaration->expression == NO_ID) continue;
        TsirIrGlobal global = {.name = tsir_source_name(b, declaration->name),
            .ty = declaration->type, .class = TSIR_IR_GLOBAL_CONST};
        if ((declaration->state & DECLARATION_OVERRIDE) != 0u) {
            global.class = TSIR_IR_GLOBAL_OVERRIDE;
            OverrideRecord *record = override_declaration(&b->source, i);
            if (!record) return tsir_source_fail(b, declaration->name.offset, "missing override identity");
            global.has_override_id = (record->flags & DOE_COMPILER_OVERRIDE_HAS_ID) != 0u;
            global.override_id = record->id;
        }
        b->globals[b->module.global_count++] = global;
    }
    return b->status == TSIR_OK;
}
static void cleanup(TsirSourceBridge *b) {
    for (size_t i = 0; b->owned_functions && i < b->module.function_count; ++i) {
        TsirSourceFunction *f = &b->owned_functions[i];
        free(f->params); free(f->locals); free(f->exprs); free(f->stmts);
        free(f->args); free(f->children); free(f->scope); free(f->local_names);
    }
    free(b->owned_functions); free(b->functions); free(b->globals);
    free(b->fields); free(b->structs); free(b->types); free(b->names);
    free(b->compiler);
}
TsirStatus tsir_source_lower(const char *source, size_t length,
    const char *frontend_version, TsirSemantic *out,
    DoeCompilerDiagnostic *diagnostic) {
    const int saved_errno = errno;
    if (!out || !diagnostic) return TSIR_ERR_INVALID;
    memset(out, 0, sizeof(*out));
    memset(diagnostic, 0, sizeof(*diagnostic));
    if (!source || !frontend_version || length > (1u << 20)) return TSIR_ERR_INVALID;
    TsirSourceBridge b = {.source = {.source = source, .length = length, .diagnostic = diagnostic}};
    /* These are explicit compiler workspace budgets, not source classifications.
     * Exhaustion keeps the compiler's pool and first diagnostic. */
    DoeCompilerModuleCapacities capacities = {.typed = {
        .source_bytes = 1u << 20, .tokens = 16384u, .statements = 2048u,
        .expressions = 8192u, .types = 256u, .members = 1024u,
        .resources = 128u, .access_steps = 16384u, .constants = 8192u,
        .constant_edges = 16384u, .ir_instructions = 65536u,
        .ir_operands = 16384u, .target_declarations = 8192u,
        .traversal_items = 1024u, .words = 0u,
        .expression_depth = 64u, .control_depth = 32u, .type_syntax_depth = 64u},
        .entries = 128u, .entry_inputs = 1024u, .entry_references = 16384u};
    CompilerLimits limits;
    size_t bytes = 0u;
    if (!compiler_module_layout(&capacities, &limits, &bytes)) return TSIR_ERR_INVALID;
    DoeCompilerSpecializationCapacities specialization_capacities = {
        .target = {.stages = {.module = capacities}},
        .overrides = capacities.typed.expressions};
    DoeCompilerWorkspaceRequirements requirements;
    size_t specialization_base;
    if (!specialization_layout(DOE_COMPILER_TARGET_SPIRV,
        &specialization_capacities, &specialization_base, &requirements))
        return TSIR_ERR_INVALID;
    b.compiler = calloc(1u, requirements.size_bytes);
    if (!b.compiler) {
        errno = saved_errno;
        return TSIR_ERR_NOMEM;
    }
    compiler_job_bind(b.compiler, b.compiler, &limits);
    /* Reuse the compiler's symbolic override owner; no specialization values
     * or target selection are supplied by semantic TSIR recovery. */
    SpecializationContext specialization = {0};
    specialization_bind(&specialization, b.compiler, specialization_base,
        &specialization_capacities);
    b.source.specialization = &specialization;
    NumericEnvironment environment;
    bool analyzed = compiler_analyze(&b.source, b.compiler, &environment);
    if (analyzed) {
        /* Every syntax name occurrence can be visited several times while
         * adapting nested access paths. Bound retained name bytes explicitly. */
        b.names_cap = (length + 1u) * 32u;
        b.names = malloc(b.names_cap);
        b.functions = calloc(b.compiler->function_count + 1u, sizeof(*b.functions));
        b.owned_functions = calloc(b.compiler->function_count + 1u, sizeof(*b.owned_functions));
        b.module.functions = b.functions;
        b.module.function_count = b.compiler->function_count;
        if (!b.names || !b.functions || !b.owned_functions || !types(&b) || !globals(&b)) {
            if (b.status == TSIR_OK) b.status = TSIR_ERR_NOMEM;
        }
        for (uint32_t i = 0u; b.status == TSIR_OK && i < b.compiler->function_count; ++i) {
            if (!tsir_source_function(&b, &b.owned_functions[i], i)) {
                if (b.status == TSIR_OK) b.status = TSIR_ERR_NOMEM;
                break;
            }
            b.functions[i] = b.owned_functions[i].view;
        }
        if (b.status == TSIR_OK) {
            uint8_t digest[32];
            tsir_sha256((const uint8_t *)source, length, digest);
            b.status = tsir_frontend_lower(&b.module, digest, frontend_version, out);
        }
    } else b.status = TSIR_ERR_INVALID;
    if (!numeric_environment_end(&b.source, &environment)) {
        tsir_frontend_free_semantic(out);
        b.status = TSIR_ERR_INVALID;
    }
    cleanup(&b);
    errno = saved_errno;
    return b.status;
}
