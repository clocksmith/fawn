/* SPDX-License-Identifier: MIT */
#ifndef DOE_POINTER_LOWER_H
#define DOE_POINTER_LOWER_H
#include "sema.h"
typedef struct {
    Source *source;
    FunctionRecord original, clone;
    uint32_t extra_formal, argument_first, argument_count, value_start;
    uint64_t *work;
} PointerClone;
bool pointer_lower(Source *);
bool pointer_clone_instruction(PointerClone *, const IrInstruction *);
uint32_t pointer_clone_value(const PointerClone *, uint32_t);
bool pointer_clone_path(PointerClone *, uint32_t, uint32_t, bool, uint32_t *);
bool pointer_lower_charge(Source *, uint64_t *, uint64_t);
#endif
