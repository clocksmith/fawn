// SPDX-License-Identifier: MIT
// emit_csl_validate.zig: pattern validation + toolchain config.
// Per-pattern substring rules and SDK 2.10 compatibility rules are
// copied exactly from Zig (see csl_validate.c). Toolchain execution
// (running cslc --version) is NOT done by this library: callers run it.
// csl_validate_toolchain returns CSL_ERR_UNSUPPORTED when asked to run
// cslc, and CSL_ERR_INVALID for an empty executable path, mirroring
// Zig's ShaderToolchainUnavailable / empty-path errors visibly.
#ifndef CSL_VALIDATE_H
#define CSL_VALIDATE_H
#include "csl_kernel.h"
typedef enum {
    CSL_VAL_ELEMENT_WISE, CSL_VAL_REDUCTION, CSL_VAL_GATHER,
    CSL_VAL_ATTENTION_DECODE, CSL_VAL_ATTENTION_TILED, CSL_VAL_ATTENTION_LINEAR,
    CSL_VAL_ATTENTION_STREAMING, CSL_VAL_TILED_MATMUL,
    CSL_VAL_TILED_MATMUL_Q4K, CSL_VAL_FUSED_GEMV_DEQUANT, CSL_VAL_DENSE_GEMV,
    CSL_VAL_DEQUANT, CSL_VAL_ROPE, CSL_VAL_SAMPLE, CSL_VAL_KV_WRITE,
    CSL_VAL_KV_READ, CSL_VAL_FUSED_FFN
} CslValidatePattern;
#define CSL_CSLC_ENV_VAR "DOE_WGSL_CSLC"
#define CSL_CSLC_PATH_SENTINEL "PATH"
typedef enum { CSL_DISC_ENV_PATH, CSL_DISC_EXPLICIT, CSL_DISC_PATH_LOOKUP } CslDiscovery;
typedef struct { const char *executable; CslDiscovery discovery; } CslToolchainConfig;
/* Returns 1 when valid; when invalid, *first_error (if non-NULL) is a
   static message string from the Zig rules. */
int csl_validate_pattern(const char *csl, CslValidatePattern pattern, const char **first_error);
CslErr csl_toolchain_config_from_env_value(const char *raw, CslToolchainConfig *out);
CslErr csl_validate_toolchain(const CslToolchainConfig *cfg);
int csl_pattern_kind_from_name(const char *name, CslValidatePattern *out);
#endif
