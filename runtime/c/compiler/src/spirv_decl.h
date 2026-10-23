/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_COMPILER_SPIRV_DECL_H
#define DOE_COMPILER_SPIRV_DECL_H
/* Closed target descriptor alternatives. The shared TargetDeclaration stores
 * the corresponding type, storage class, source span index and scalar payload. */
typedef enum {
    DECL_TYPE, DECL_POINTER, DECL_WRAPPER, DECL_U32, DECL_ZERO,
    DECL_LEAF, DECL_COMPOSITE, DECL_SAMPLED_IMAGE
} SpirvDeclarationKind;
#endif
