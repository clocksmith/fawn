/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "spirv_internal.h"

/* Opt-in write marks (DOE_COMPILER_WRITE_MARKS, SPIR-V only).
 *
 * Every store or atomic on a read_write storage buffer also stores 1 into the
 * hidden mark word of each 64 KiB page the access touches, and every
 * textureStore on a 2D storage texture marks its 256x256 texel tile. The mark
 * buffer is one hidden storage binding (DOE_COMPILER_WRITE_MARK_GROUP/BINDING);
 * a hidden immediate block carries two words per marked slot:
 *   word 2k     first mark index of slot k's region,
 *   word 2k + 1 binding byte offset (buffer) or tiles per row (texture).
 * Slot k is the source-order rank among the module's writable storage
 * resources; the consumer derives the same order from module reflection.
 * Indices are clamped to the last mark word, which the consumer keeps as a
 * sink. A mark is a plain idempotent store: a race between invocations only
 * repeats it, and over-marking costs restore bytes, never correctness. */

enum { MARK_PAGE_BYTES = 1u << DOE_COMPILER_WRITE_MARK_PAGE_SHIFT };

static bool marks_enabled(const Emitter *e)
{
    return (e->program->module_flags & MODULE_WRITE_MARKS) != 0u;
}

static bool writable_resource(const ResourceRecord *r)
{
    return (r->kind == DOE_COMPILER_STORAGE_BUFFER && r->access == DOE_COMPILER_READ_WRITE) ||
        (r->kind == DOE_COMPILER_STORAGE_TEXTURE &&
         (r->access == DOE_COMPILER_WRITE || r->access == DOE_COMPILER_READ_WRITE));
}

/* Source-order rank of a writable resource among the module's writable ones. */
static uint32_t mark_slot(const CompilerWorkspace *c, uint32_t resource)
{
    uint32_t slot = 0u;
    for (uint32_t i = 0u; i < resource; ++i)
        if (writable_resource(&c->resources[i])) ++slot;
    return slot;
}

static const ResourceRecord *marked_buffer(const Emitter *e, const IrInstruction *ir)
{
    const CompilerWorkspace *c = e->program;
    const bool write = ir->opcode == IR_ACCESS_STORE ||
        (atomic_opcode(ir->opcode) && ir->opcode != IR_ATOMIC_LOAD);
    if (!marks_enabled(e) || !write || ir->operands.access.category != ROOT_RESOURCE ||
        ir->operands.access.root >= c->resource_count) return NULL;
    const ResourceRecord *r = &c->resources[ir->operands.access.root];
    return r->kind == DOE_COMPILER_STORAGE_BUFFER && r->access == DOE_COMPILER_READ_WRITE ? r : NULL;
}

/* Byte size and alignment of the stored value. */
static uint64_t stored_size(const CompilerWorkspace *c, const IrInstruction *ir)
{
    return atomic_opcode(ir->opcode) ? 4u : type_size(c, ir->type);
}
static uint64_t stored_alignment(const CompilerWorkspace *c, const IrInstruction *ir)
{
    return atomic_opcode(ir->opcode) ? 4u : type_alignment(c, ir->type);
}

/* One access step's byte contribution: a constant, or index * stride. */
static bool step_layout(Emitter *e, const AccessStep *a, uint64_t *constant, uint64_t *stride)
{
    const CompilerWorkspace *c = e->program;
    *constant = 0u;
    *stride = 0u;
    if (a->input_type < IR_PRIMITIVE_COUNT) return spirv_emit_error(e, a->source_offset, "write mark on scalar access step");
    const TypeRecord *input = &c->types[a->input_type - IR_PRIMITIVE_COUNT];
    if (a->kind == ACCESS_MEMBER) {
        if (input->kind != TYPE_STRUCT || a->index >= input->count)
            return spirv_emit_error(e, a->source_offset, "write mark member step outside its structure");
        *constant = c->members[input->element + a->index].offset;
        return true;
    }
    uint64_t element;
    switch (input->kind) {
    case TYPE_ARRAY: case TYPE_RUNTIME_ARRAY: element = input->state.layout.stride; break;
    case TYPE_VECTOR: element = type_size(c, input->element); break;
    case TYPE_MATRIX: element = type_alignment(c, input->element); break;
    default: return spirv_emit_error(e, a->source_offset, "write mark on an unsupported access step");
    }
    if (a->kind == ACCESS_COMPONENT) *constant = (uint64_t)a->index * element;
    else *stride = element;
    return true;
}

static bool constant_word(Emitter *e, uint64_t value, uint32_t offset, uint32_t *id)
{
    if (value > UINT32_MAX) return spirv_emit_error(e, offset, "write mark offset exceeds 32 bits");
    *id = spirv_unsigned_constant(e, (uint32_t)value);
    return *id != 0u;
}

/* The constant byte part of a path; strides are planned as constants too. */
static bool path_constant(Emitter *e, uint32_t path, bool plan, uint64_t *total)
{
    const CompilerWorkspace *c = e->program;
    *total = 0u;
    for (uint32_t step = path; step != NO_ID; step = c->accesses[step].parent) {
        if (step >= c->access_count) return spirv_emit_error(e, 0u, "invalid write mark access path");
        uint64_t constant, stride;
        if (!step_layout(e, &c->accesses[step], &constant, &stride)) return false;
        if (constant > UINT32_MAX - *total) return spirv_emit_error(e, c->accesses[step].source_offset, "write mark offset exceeds 32 bits");
        *total += constant;
        uint32_t unused;
        if (plan && stride != 0u && !constant_word(e, stride, c->accesses[step].source_offset, &unused)) return false;
    }
    return true;
}

static bool admit_store(Emitter *e, const IrInstruction *ir)
{
    const uint64_t size = stored_size(e->program, ir);
    if (size == 0u || size > MARK_PAGE_BYTES)
        return spirv_emit_error(e, ir->source_offset, "write marks refuse a storage store larger than one 64 KiB page");
    return true;
}

static bool plan_slot_words(Emitter *e, uint32_t slot)
{
    return spirv_unsigned_constant(e, 2u * slot) != 0u && spirv_unsigned_constant(e, 2u * slot + 1u) != 0u;
}

bool spirv_marks_plan_module(Emitter *e)
{
    CompilerWorkspace *c = e->program;
    e->marks = (SpirvMarks){0};
    if (!marks_enabled(e)) return true;
    uint32_t count = 0u;
    for (uint32_t i = 0u; i < c->resource_count; ++i) {
        const ResourceRecord *r = &c->resources[i];
        if (!writable_resource(r)) continue;
        if (r->group == DOE_COMPILER_WRITE_MARK_GROUP && r->binding == DOE_COMPILER_WRITE_MARK_BINDING)
            return spirv_emit_error(e, r->source_offset, "binding is reserved for write marks");
        ++count;
    }
    if (count == 0u) return true;
    if (count > DOE_COMPILER_WRITE_MARK_MAX_SLOTS)
        return spirv_emit_error(e, 0u, "write marks support at most 8 writable storage resources per module");
    for (uint32_t i = 0u; i < c->workgroup_count; ++i)
        if (c->workgroups[i].state.resolved.address_space == MODULE_IMMEDIATE)
            return spirv_emit_error(e, 0u, "write marks use the immediate range; var<immediate> is refused");
    e->marks.count = count;
    e->marks.length = spirv_unsigned_constant(e, 2u * count);
    if (e->marks.length == 0u || spirv_unsigned_constant(e, 0u) == 0u || spirv_unsigned_constant(e, 1u) == 0u ||
        spirv_unsigned_constant(e, DOE_COMPILER_WRITE_MARK_PAGE_SHIFT) == 0u ||
        spirv_unsigned_constant(e, DOE_COMPILER_WRITE_MARK_TILE_SHIFT) == 0u) return false;
    e->marks.element_pointer = spirv_pointer_type(e, IR_U32, 12u);
    if (e->marks.element_pointer == 0u) return false;
    e->marks.runtime_array = spirv_reserve_ids(e, 1u);
    e->marks.block = spirv_reserve_ids(e, 1u);
    e->marks.pointer = spirv_reserve_ids(e, 1u);
    e->marks.variable = spirv_reserve_ids(e, 1u);
    e->marks.bases_array = spirv_reserve_ids(e, 1u);
    e->marks.bases_block = spirv_reserve_ids(e, 1u);
    e->marks.bases_pointer = spirv_reserve_ids(e, 1u);
    e->marks.bases_variable = spirv_reserve_ids(e, 1u);
    e->marks.bases_element_pointer = spirv_reserve_ids(e, 1u);
    return !e->failed;
}

bool spirv_marks_plan(Emitter *e, const IrInstruction *ir)
{
    if (e->marks.count == 0u) return true;
    const CompilerWorkspace *c = e->program;
    const ResourceRecord *r = marked_buffer(e, ir);
    if (r != NULL) {
        if (!admit_store(e, ir) || !plan_slot_words(e, mark_slot(c, ir->operands.access.root))) return false;
        uint64_t constant;
        if (!path_constant(e, ir->operands.access.path, true, &constant)) return false;
        uint32_t unused;
        if (constant != 0u && !constant_word(e, constant, ir->source_offset, &unused)) return false;
        if (stored_size(c, ir) > stored_alignment(c, ir) &&
            !constant_word(e, stored_size(c, ir) - 1u, ir->source_offset, &unused)) return false;
        return true;
    }
    if (ir->opcode != IR_IMAGE || ir->operands.composite.kind != IMAGE_STORE) return true;
    const uint32_t handle = c->typed_operands[ir->operands.composite.first];
    if (handle == 0u || handle > c->instruction_count || c->instructions[handle - 1u].opcode != IR_HANDLE)
        return spirv_emit_error(e, ir->source_offset, "write marks refuse textureStore through a function parameter");
    const uint32_t resource = c->instructions[handle - 1u].operands.scalar.binding;
    const ImageType image = c->types[ir->operands.composite.reserved - IR_PRIMITIVE_COUNT].state.image;
    if (resource >= c->resource_count || image.dimension != DOE_COMPILER_VIEW_2D)
        return spirv_emit_error(e, ir->source_offset, "write marks support textureStore on 2D storage textures only");
    return plan_slot_words(e, mark_slot(c, resource));
}

bool spirv_marks_annotations(Emitter *e)
{
    if (e->marks.count == 0u) return true;
    EMIT(OP_DECORATE, e->marks.runtime_array, 6u, 4u);
    EMIT(OP_DECORATE, e->marks.block, 2u);
    EMIT(OP_MEMBER_DECORATE, e->marks.block, 0u, 35u, 0u);
    EMIT(OP_DECORATE, e->marks.variable, 34u, DOE_COMPILER_WRITE_MARK_GROUP);
    EMIT(OP_DECORATE, e->marks.variable, 33u, DOE_COMPILER_WRITE_MARK_BINDING);
    EMIT(OP_DECORATE, e->marks.bases_array, 6u, 4u);
    EMIT(OP_DECORATE, e->marks.bases_block, 2u);
    EMIT(OP_MEMBER_DECORATE, e->marks.bases_block, 0u, 35u, 0u);
    return true;
}

bool spirv_marks_declarations(Emitter *e)
{
    if (e->marks.count == 0u) return true;
    EMIT(OP_TYPE_RUNTIME_ARRAY, e->marks.runtime_array, ID_U32);
    EMIT(OP_TYPE_STRUCT, e->marks.block, e->marks.runtime_array);
    EMIT(OP_TYPE_POINTER, e->marks.pointer, 12u, e->marks.block);
    EMIT(OP_TYPE_ARRAY, e->marks.bases_array, ID_U32, e->marks.length);
    EMIT(OP_TYPE_STRUCT, e->marks.bases_block, e->marks.bases_array);
    EMIT(OP_TYPE_POINTER, e->marks.bases_pointer, MODULE_IMMEDIATE, e->marks.bases_block);
    EMIT(OP_TYPE_POINTER, e->marks.bases_element_pointer, MODULE_IMMEDIATE, ID_U32);
    return true;
}

bool spirv_marks_variables(Emitter *e)
{
    if (e->marks.count == 0u) return true;
    EMIT(OP_VARIABLE, e->marks.pointer, e->marks.variable, 12u);
    EMIT(OP_VARIABLE, e->marks.bases_pointer, e->marks.bases_variable, MODULE_IMMEDIATE);
    return true;
}

static uint32_t binary(Emitter *e, uint32_t opcode, uint32_t left, uint32_t right)
{
    const uint32_t result = spirv_reserve_ids(e, 1u);
    const uint32_t arguments[] = {ID_U32, result, left, right};
    return spirv_instruction(e, opcode, arguments, 4u) ? result : 0u;
}

static uint32_t base_word(Emitter *e, uint32_t word)
{
    const uint32_t pointer = spirv_reserve_ids(e, 1u), value = spirv_reserve_ids(e, 1u);
    const uint32_t chain[] = {e->marks.bases_element_pointer, pointer, e->marks.bases_variable,
        spirv_unsigned_constant(e, 0u), spirv_unsigned_constant(e, word)};
    const uint32_t load[] = {ID_U32, value, pointer};
    return spirv_instruction(e, OP_ACCESS_CHAIN, chain, 5u) && spirv_instruction(e, OP_LOAD, load, 3u) ? value : 0u;
}

/* marks[min(index, length - 1)] = 1: the last word is the consumer's sink. */
static bool store_mark(Emitter *e, uint32_t index)
{
    const uint32_t length = spirv_reserve_ids(e, 1u);
    EMIT(OP_ARRAY_LENGTH, ID_U32, length, e->marks.variable, 0u);
    const uint32_t last = binary(e, OP_ISUB, length, spirv_unsigned_constant(e, 1u));
    const uint32_t inside = spirv_reserve_ids(e, 1u);
    EMIT(OP_ULESS_THAN, ID_BOOL, inside, index, last);
    const uint32_t clamped = spirv_reserve_ids(e, 1u);
    EMIT(OP_SELECT, ID_U32, clamped, inside, index, last);
    const uint32_t pointer = spirv_reserve_ids(e, 1u);
    EMIT(OP_ACCESS_CHAIN, e->marks.element_pointer, pointer, e->marks.variable, spirv_unsigned_constant(e, 0u), clamped);
    EMIT(OP_STORE, pointer, spirv_unsigned_constant(e, 1u));
    return !e->failed;
}

static bool mark_page(Emitter *e, uint32_t first, uint32_t byte)
{
    const uint32_t page = binary(e, OP_SHIFT_RIGHT_LOGICAL, byte, spirv_unsigned_constant(e, DOE_COMPILER_WRITE_MARK_PAGE_SHIFT));
    const uint32_t index = binary(e, OP_IADD, first, page);
    return !e->failed && store_mark(e, index);
}

/* Called inside the store's guarded block, after the store, with c->walk still
 * holding the access chain's resolved u32 cursors. */
bool spirv_marks_buffer(Emitter *e, const IrInstruction *ir)
{
    if (e->marks.count == 0u || marked_buffer(e, ir) == NULL) return true;
    CompilerWorkspace *c = e->program;
    const uint32_t slot = mark_slot(c, ir->operands.access.root);
    uint64_t constant = 0u;
    uint32_t offset = 0u;
    for (uint32_t i = c->walk_count; i != 0u; --i) {
        const WalkItem *w = &c->walk[i - 1u];
        uint64_t part, stride;
        if (!step_layout(e, &c->accesses[w->object], &part, &stride)) return false;
        constant += part;
        if (stride == 0u) continue;
        uint32_t stride_id;
        if (!constant_word(e, stride, ir->source_offset, &stride_id)) return false;
        const uint32_t term = binary(e, OP_IMUL, w->cursor, stride_id);
        offset = offset == 0u ? term : binary(e, OP_IADD, offset, term);
    }
    if (constant != 0u) {
        uint32_t constant_id;
        if (!constant_word(e, constant, ir->source_offset, &constant_id)) return false;
        offset = offset == 0u ? constant_id : binary(e, OP_IADD, offset, constant_id);
    }
    const uint32_t first = base_word(e, 2u * slot), binding_offset = base_word(e, 2u * slot + 1u);
    const uint32_t start = offset == 0u ? binding_offset : binary(e, OP_IADD, binding_offset, offset);
    if (e->failed || !mark_page(e, first, start)) return false;
    if (stored_size(c, ir) <= stored_alignment(c, ir)) return true;
    uint32_t tail;
    if (!constant_word(e, stored_size(c, ir) - 1u, ir->source_offset, &tail)) return false;
    return mark_page(e, first, binary(e, OP_IADD, start, tail));
}

/* Called after OpImageWrite inside its bounds guard; coordinate is the
 * admitted u32 vec2. tile = first + (y >> 8) * tiles_per_row + (x >> 8). */
bool spirv_marks_texel(Emitter *e, const IrInstruction *ir, uint32_t coordinate)
{
    if (e->marks.count == 0u || ir->operands.composite.kind != IMAGE_STORE) return true;
    const CompilerWorkspace *c = e->program;
    const uint32_t handle = c->typed_operands[ir->operands.composite.first];
    const uint32_t slot = mark_slot(c, c->instructions[handle - 1u].operands.scalar.binding);
    const uint32_t x = spirv_reserve_ids(e, 1u), y = spirv_reserve_ids(e, 1u);
    EMIT(OP_COMPOSITE_EXTRACT, ID_U32, x, coordinate, 0u);
    EMIT(OP_COMPOSITE_EXTRACT, ID_U32, y, coordinate, 1u);
    const uint32_t shift = spirv_unsigned_constant(e, DOE_COMPILER_WRITE_MARK_TILE_SHIFT);
    const uint32_t column = binary(e, OP_SHIFT_RIGHT_LOGICAL, x, shift);
    const uint32_t row = binary(e, OP_SHIFT_RIGHT_LOGICAL, y, shift);
    const uint32_t first = base_word(e, 2u * slot), per_row = base_word(e, 2u * slot + 1u);
    const uint32_t tile = binary(e, OP_IADD, binary(e, OP_IMUL, row, per_row), column);
    return !e->failed && store_mark(e, binary(e, OP_IADD, first, tile));
}

/* Each marked store adds at most this many IDs beyond its own access. */
uint64_t spirv_marks_id_budget(const CompilerWorkspace *c)
{
    if ((c->module_flags & MODULE_WRITE_MARKS) == 0u) return 0u;
    return 40u * (uint64_t)c->limits.instructions + 2u * (uint64_t)c->limits.accesses + 16u;
}
