// SPDX-License-Identifier: MIT
// emit_csl_toolchain.zig: Makefile emission, compile-plan and
// simulator-plan JSON wrappers. Makefile rules follow Zig's shape:
// cslc header, fabric dims, per-target rules with --memcpy
// --channels=1, an all target, a clean target, and troubleshooting
// notes. The exact Zig Makefile text was not reproduced byte-for-byte;
// this is a structural equivalent with the same flags and targets.
#ifndef CSL_TOOLCHAIN_H
#define CSL_TOOLCHAIN_H
#include "csl_host_plan.h"
#include "csl_simulator.h"
CslErr csl_emit_makefile(CslBuf *out, const CslCompileTarget *targets, size_t target_count, uint32_t fabric_width, uint32_t fabric_height);
CslErr csl_emit_compile_plan_json(CslBuf *out, const CslHostPlan *plan, const CslCompileTarget *targets, size_t target_count);
CslErr csl_emit_simulator_plan_wrapper_json(CslBuf *out, const CslSimulatorPlan *plan);
#endif
