/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_DXIL_EMIT_H
#define DOE_DXIL_EMIT_H

#include <stddef.h>
#include <stdint.h>

#include "dxil_diagnostic.h"
#include "dxil_error.h"
#include "dxil_ir.h"
#include "dxil_mapping.h"
#include "dxil_toolchain.h"

/* Primary DXIL emission and the DXC fallback path, porting
 * emit_dxil.zig. There is no thread-local diagnostic in C: every
 * function takes an explicit DxilDiagnostic pointer, and NULL means
 * "report the status, drop the message". The `_with_diagnostic`
 * spellings take a diagnostic that must be non-NULL in spirit (the
 * implementation still tolerates NULL); the plain spellings are the
 * C analogues of Zig's compatibility-diagnostic variants and pass a
 * nullable diagnostic through.
 *
 * Zig behavior preserved: emit-path diagnostic messages always carry
 * kind InvalidIr (Zig's setLastErrorFmt hard-codes error.InvalidIr),
 * even when the returned status is ShaderToolchainUnavailable; the
 * validation gate message is "{producer} produced a structurally
 * invalid DXIL container: {detail}"; output larger than the caller
 * buffer fails with OutputTooLarge and no diagnostic detail. */

/* Lowered entry point (emit_dxil.zig's Module): the first entry
 * point's stage, the IR function name (borrowed), its workgroup
 * size, and the default shader model 6.0. */
typedef struct DxilLoweredModule {
    DxilStage stage;
    const char *name;
    uint32_t workgroup_size[3];
    unsigned shader_model_major;
    unsigned shader_model_minor;
} DxilLoweredModule;

DxilStatus dxil_lower(const DxilIrModule *module,
    DxilLoweredModule *lowered);

/* Primary path: native emit plus the validation gate with producer
 * "native emitter". */
DxilStatus dxil_emit_with_diagnostic(const DxilIrModule *module,
    uint8_t *out, size_t out_len, size_t *written,
    DxilDiagnostic *diagnostic);
DxilStatus dxil_emit(const DxilIrModule *module, uint8_t *out,
    size_t out_len, size_t *written, DxilDiagnostic *diagnostic);

/* DXC toolchain configuration for the fallback path: an executable
 * string (borrowed) plus a discovery value from dxil_toolchain.h. */
typedef struct DxilToolchainConfig {
    const char *executable;
    DxilDiscovery discovery;
} DxilToolchainConfig;

/* DXC fallback path: lower, generate HLSL into a 256 KiB stack
 * buffer, write it to a unique file under cache_dir (default
 * ".zig-cache/doe_wgsl_dxil" when cache_dir is NULL), run DXC, read
 * the output back (at most 256 KiB), validate it with producer
 * "DXC", and copy it to the caller buffer. Both temporary files are
 * deleted before returning on every path. The unique file suffix is
 * a process id plus counter created with O_EXCL; Zig uses a
 * nanosecond timestamp nonce instead. */
DxilStatus dxil_emit_toolchain_with_diagnostic(
    const DxilIrModule *module, uint8_t *out, size_t out_len,
    const DxilToolchainConfig *config, const char *cache_dir,
    size_t *written, DxilDiagnostic *diagnostic);
DxilStatus dxil_emit_toolchain(const DxilIrModule *module, uint8_t *out,
    size_t out_len, const DxilToolchainConfig *config,
    const char *cache_dir, size_t *written,
    DxilDiagnostic *diagnostic);

#endif /* DOE_DXIL_EMIT_H */
