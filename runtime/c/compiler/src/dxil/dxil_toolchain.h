/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_DXIL_TOOLCHAIN_H
#define DOE_DXIL_TOOLCHAIN_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "dxil_error.h"
#include "dxil_mapping.h"

/* DXC toolchain configuration and invocation. Faithful port of the
 * portable parts of emit_dxil.zig plus toolchain.zig's discovery
 * labels and diagnostic-output rule, and of the error mapping in
 * pipeline/translate_dxil.zig.
 *
 * Not ported here: the primary emit path (needs Doe IR and the native
 * emitter), the HLSL-generation step of the DXC fallback (needs the
 * HLSL emitter), and temporary-file management. The DXC runner below
 * takes already-written HLSL input and DXIL output paths, which is
 * the exact boundary where the HLSL emitter hands off. */

#define DXIL_DXC_ENV_VAR "DOE_WGSL_DXC"
#define DXIL_DXC_PATH_SENTINEL "PATH"
#define DXIL_DXC_MAX_OUTPUT_BYTES (64u * 1024u)
/* Zig formats profiles into a 16-byte buffer; a longer result is
 * InvalidIr there and here. */
#define DXIL_DXC_PROFILE_CAP 16u

typedef enum DxilDiscovery {
    DXIL_DISCOVERY_EXPLICIT_CONFIG,
    DXIL_DISCOVERY_ENV_PATH,
    DXIL_DISCOVERY_ENV_PATH_LOOKUP,
    DXIL_DISCOVERY_IMPLICIT_PATH_LOOKUP
} DxilDiscovery;

/* Discovery label strings from toolchain.zig. */
const char *dxil_discovery_label(DxilDiscovery discovery);

/* Platform default DXC executable: "dxc.exe" on Windows, "dxc"
 * elsewhere. */
const char *dxil_default_dxc_executable(void);

/* Parses one DOE_WGSL_DXC value. Trims spaces, tabs, CR, and LF from
 * both ends. An empty value fails with
 * DXIL_SHADER_TOOLCHAIN_UNAVAILABLE and a message naming the env var
 * and the PATH sentinel (written to errbuf when it fits). The literal
 * "PATH" (any case) selects env_path_lookup with the default
 * executable; anything else selects env_path with the trimmed value.
 * The executable is written to exe_buf (NUL-terminated); exe_buf too
 * small fails with DXIL_OUTPUT_TOO_LARGE. */
DxilStatus dxil_toolchain_from_env_value(const char *raw_value,
    char *exe_buf, size_t exe_cap, DxilDiscovery *discovery,
    char *errbuf, size_t errcap);

/* Reads DOE_WGSL_DXC from the environment. An unset variable selects
 * implicit_path_lookup with the default executable. */
DxilStatus dxil_toolchain_from_env(char *exe_buf, size_t exe_cap,
    DxilDiscovery *discovery, char *errbuf, size_t errcap);

/* Formats "{cs|vs|ps}_{major}_{minor}" into buf. Fails with
 * DXIL_INVALID_IR when the result would not fit DXIL_DXC_PROFILE_CAP
 * bytes including the terminator. */
DxilStatus dxil_toolchain_profile(DxilStage stage, unsigned major,
    unsigned minor, char *buf, size_t buf_cap);

/* Fills argv with the DXC argument vector (plus a NULL terminator for
 * exec): executable, "-T", profile, "-E", entry name, "-Fo", output
 * path, input path. All pointed-to strings are borrowed from the
 * caller. */
void dxil_dxc_argv(const char *executable, const char *profile,
    const char *entry_name, const char *output_path,
    const char *input_path, const char *argv[9]);

/* Preserve the legacy argument prefix and append the native-half option only
 * when required by the typed HLSL entry metadata. The caller must select a
 * compatible shader model (at least 6.2 for native 16-bit scalar types). */
void dxil_dxc_argv_options(const char *executable, const char *profile,
    const char *entry_name, const char *output_path, const char *input_path,
    bool native16, const char *argv[10]);

/* Runs DXC once. POSIX uses fork/exec; Windows uses CreateProcessW
 * without a shell, with UTF-8 arguments and a restricted inherited-handle
 * list. Each output stream retains at most DXIL_DXC_MAX_OUTPUT_BYTES:
 * spawn failure (including executable not found) gives
 * DXIL_SHADER_TOOLCHAIN_UNAVAILABLE with a discovery-specific
 * message; a nonzero exit or termination by a signal gives
 * DXIL_INVALID_IR with a message containing the discovery label,
 * executable, exit code or signal, profile, and entry name, plus the
 * captured detail (trimmed stderr, else trimmed stdout) when
 * non-empty. Windows drains both streams concurrently, discarding excess
 * bytes after the cap, and reports the complete unsigned process exit code.
 * Its transient OS attribute allocation is released before returning;
 * caller input, workspace and output ownership do not change.
 * Messages are written to errbuf (truncated, NUL-terminated, when the
 * buffer is too small); errbuf may be NULL only when errcap is 0. */
DxilStatus dxil_dxc_run(const char *executable, const char *profile,
    const char *entry_name, const char *output_path,
    const char *input_path, DxilDiscovery discovery,
    char *errbuf, size_t errcap);

/* Same process/error/ownership contract, with an explicit typed native-half
 * requirement. The legacy spelling forwards false and keeps its exact argv. */
DxilStatus dxil_dxc_run_options(const char *executable, const char *profile,
    const char *entry_name, const char *output_path,
    const char *input_path, DxilDiscovery discovery, bool native16,
    char *errbuf, size_t errcap);

/* The output validation gate from emit_dxil.zig: validates container
 * bytes and, on failure, returns DXIL_INVALID_IR with the message
 * "{producer} produced a structurally invalid DXIL container:
 * {detail}". Producer is "native emitter" or "DXC" at the call sites. */
DxilStatus dxil_validate_output(const uint8_t *bytes, size_t len,
    const char *producer, char *errbuf, size_t errcap);

/* The translate layer's error mapping (translate_dxil.zig). The C
 * status set is already shared between the emitter and the translate
 * layer, so the mapping is the identity; it exists so call sites name
 * the translation step explicitly instead of passing errors through
 * silently. */
DxilStatus dxil_emit_error_to_translate_error(DxilStatus status);

/* Re-exports for translate call sites (same values as the emitter). */
#define DXIL_TRANSLATE_MAX_OUTPUT DXIL_MAX_OUTPUT

#endif /* DOE_DXIL_TOOLCHAIN_H */
