/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_TSIR_H
#define DOE_TSIR_H

/* Public non-CSL TSIR facade, corresponding to compiler/tsir/mod.zig.
 * Types and behavior stay with their included owners; this facade owns no
 * state, alternate implementations, or implicit backend selection. Read the
 * individual headers for borrowing, allocation and failure contracts.
 * CSL remains excluded by config/csl-quarantine.json.
 */
#include "tsir_schema.h"
#include "tsir_digest.h"
#include "tsir_reference.h"
#include "tsir_frontend.h"
#include "tsir_planner.h"
#include "tsir_emit.h"
#include "tsir_bootstrap.h"
#include "tsir_bootstrap_manifest.h"
#include "tsir_source.h"
#include "tsir_targets.h"

#endif
