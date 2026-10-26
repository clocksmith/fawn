// SPDX-License-Identifier: MIT
// See header for the structural-equivalent note.
#include "csl_toolchain.h"
#include "csl_emit_common.h"


CslErr csl_emit_makefile(CslBuf *out, const CslCompileTarget *targets, size_t target_count, uint32_t fabric_width, uint32_t fabric_height) {
    size_t i;
    if (!out || !targets) return CSL_ERR_INVALID;
    PUT("# CSL Makefile (generated; cslc toolchain)\n");
    PUT("CSLC ?= cslc\n");
    PUT("FABRIC_DIMS = --fabric-dims ");
    PUTI(fabric_width); PUT("x"); PUTI(fabric_height); PUT("\n\n");
    PUT("all:");
    for (i = 0; i < target_count; i++) { PUT(" "); PUT(targets[i].name ? targets[i].name : "target"); }
    PUT("\n\n");
    for (i = 0; i < target_count; i++) {
        PUT(targets[i].name ? targets[i].name : "target"); PUT(": ");
        PUT(targets[i].layout ? targets[i].layout : "layout.csl"); PUT(" ");
        PUT(targets[i].pe_program ? targets[i].pe_program : "pe_program.csl"); PUT("\n");
        PUT("\t$(CSLC) --memcpy --channels=1 --fabric-dims ");
        PUTI(fabric_width); PUT("x"); PUTI(fabric_height); PUT(" ");
        PUT(targets[i].layout ? targets[i].layout : "layout.csl"); PUT("\n\n");
    }
    PUT("clean:\n\trm -f *.elf *.o\n\n");
    PUT("# Troubleshooting: if cslc is not found, set CSLC to its path or\n");
    PUT("# set DOE_WGSL_CSLC to a cslc path or PATH for PATH lookup.\n");
    return CSL_OK;
}
CslErr csl_emit_compile_plan_json(CslBuf *out, const CslHostPlan *plan, const CslCompileTarget *targets, size_t target_count) {
    if (!out || !plan) return CSL_ERR_INVALID;
    return csl_emit_host_plan_artifact_json(out, plan, targets, target_count, NULL);
}
CslErr csl_emit_simulator_plan_wrapper_json(CslBuf *out, const CslSimulatorPlan *plan) {
    if (!out || !plan) return CSL_ERR_INVALID;
    return csl_emit_simulator_plan_json(out, plan);
}
