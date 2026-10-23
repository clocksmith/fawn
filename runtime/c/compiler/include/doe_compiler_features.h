/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_COMPILER_FEATURES_H
#define DOE_COMPILER_FEATURES_H
#include "doe_compiler_module.h"

/* Additive input-mask meanings for the experimental subgroup extension.
 * The frozen ABI5 declarations and record layouts remain unchanged.
 * These are source interface facts, not device-feature authorization. */
#define DOE_COMPILER_INPUT_SUBGROUP_SIZE 32u
#define DOE_COMPILER_INPUT_SUBGROUP_INVOCATION_ID 64u
#define DOE_COMPILER_INPUT_NUM_SUBGROUPS 128u
#define DOE_COMPILER_INPUT_SUBGROUP_ID 256u

/* Generated SPIR-V capabilities must be checked against the selected physical
 * device and compute-stage support before creating or executing a pipeline.
 * Successful compilation/format validation does not establish that support. */
#endif
