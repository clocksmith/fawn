/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "test_util.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "test_bitcode_scan.h"
#include "dxil_analyze.h"
#include "dxil_diagnostic.h"
#include "dxil_ir.h"
#include "dxil_translate.h"
#include "dxil_validate.h"

static const char *canonical_source =
    "@compute @workgroup_size(64, 1, 1) "
    "fn main(@builtin(global_invocation_id) gid: vec3<u32>) {}";

static bool translate_source(const char *source, uint8_t *out,
    size_t out_len, size_t *written, DxilDiagnostic *diagnostic,
    DxilDiagKind *err) {
    return dxil_translate(source, strlen(source), out, out_len,
        written, diagnostic, err);
}

static void check_canonical(void) {
    static uint8_t out[65536];
    size_t written = 0;
    DxilDiagnostic diagnostic;
    DxilDiagKind err = DXIL_DIAG_INVALID_IR;
    DxilValidationResult result;
    const uint8_t *bitcode = NULL;
    size_t bitcode_len = 0;
    DxilScanResult scan;
    dxil_diagnostic_clear(&diagnostic);
    CHECK(translate_source(canonical_source, out, sizeof(out),
        &written, &diagnostic, &err));
    CHECK(written > 32);
    CHECK(memcmp(out, "DXBC", 4) == 0);
    dxil_validate(out, written, &result);
    CHECK(result.valid);
    CHECK(result.has_shader_kind && result.shader_kind == 5);
    CHECK(result.part_count == 4);
    CHECK(container_bitcode(out, written, &bitcode, &bitcode_len));
    CHECK(scan_bitcode(bitcode, bitcode_len, &scan));
    CHECK(scan.ok);
    CHECK(scan.function_blocks == 1);
    CHECK(scan.call_records == 3);
    CHECK(scan.function_saw_ret_last);
}

static void check_storage_and_vertex(void) {
    static uint8_t out[65536];
    size_t written = 0;
    DxilDiagnostic diagnostic;
    DxilDiagKind err = DXIL_DIAG_INVALID_IR;
    DxilValidationResult result;
    const char *storage_source =
        "@group(0) @binding(0) var<storage, read_write> "
        "buf: array<u32, 4>; "
        "@compute @workgroup_size(64) "
        "fn main(@builtin(global_invocation_id) gid: vec3<u32>) { "
        "workgroupBarrier(); buf[gid.x] = gid.x + 1u; }";
    const char *vertex_source =
        "@vertex fn main(@builtin(vertex_index) vi: u32) "
        "-> @builtin(position) vec4<f32> { "
        "return vec4<f32>(0.0, 0.0, 0.0, 1.0); }";
    dxil_diagnostic_clear(&diagnostic);
    CHECK(translate_source(storage_source, out, sizeof(out),
        &written, &diagnostic, &err));
    dxil_validate(out, written, &result);
    CHECK(result.valid);
    CHECK(result.has_shader_kind && result.shader_kind == 5);
    dxil_diagnostic_clear(&diagnostic);
    CHECK(translate_source(vertex_source, out, sizeof(out),
        &written, &diagnostic, &err));
    dxil_validate(out, written, &result);
    CHECK(result.valid);
    CHECK(result.has_shader_kind && result.shader_kind == 1);
}

static void check_analyze_errors(void) {
    static uint8_t out[65536];
    size_t written = 0;
    DxilDiagnostic diagnostic;
    DxilDiagKind err = DXIL_DIAG_INVALID_IR;
    DxilIrModule module;
    static char names[16384];
    dxil_diagnostic_clear(&diagnostic);
    CHECK(!translate_source(
        "@compute @workgroup_size(1) fn main() { "
        "let x = missing_name; }",
        out, sizeof(out), &written, &diagnostic, &err));
    CHECK(err == DXIL_DIAG_UNKNOWN_IDENTIFIER);
    CHECK(strstr(diagnostic.message, "sema") != NULL);
    dxil_diagnostic_clear(&diagnostic);
    CHECK(!translate_source("@compute fn main( {", out,
        sizeof(out), &written, &diagnostic, &err));
    CHECK(err == DXIL_DIAG_UNEXPECTED_TOKEN);
    dxil_diagnostic_clear(&diagnostic);
    CHECK(!translate_source("fn main(x: nosuchtype) {}", out,
        sizeof(out), &written, &diagnostic, &err));
    CHECK(err == DXIL_DIAG_UNKNOWN_TYPE);
    dxil_diagnostic_clear(&diagnostic);
    CHECK(!translate_source(
        "fn helper() {} fn helper() {}", out, sizeof(out),
        &written, &diagnostic, &err));
    CHECK(err == DXIL_DIAG_DUPLICATE_SYMBOL);
    dxil_diagnostic_clear(&diagnostic);
    CHECK(dxil_analyze("", 0, &module, names, sizeof(names),
        &diagnostic, &err));
    CHECK(module.entry_point_count == 0);
    dxil_diagnostic_clear(&diagnostic);
    CHECK(!translate_source("fn helper() {}", out, sizeof(out),
        &written, &diagnostic, &err));
    CHECK(err == DXIL_DIAG_INVALID_IR);
    CHECK(strstr(diagnostic.message, "dxil_emit: InvalidIr")
        != NULL);
}

static bool write_text_file(const char *path, const char *text) {
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        return false;
    }
    if (fputs(text, file) == EOF) {
        fclose(file);
        return false;
    }
    return fclose(file) == 0;
}

static bool write_bytes_file(const char *path, const uint8_t *data,
    size_t len) {
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        return false;
    }
    if (len > 0 && fwrite(data, 1, len, file) != len) {
        fclose(file);
        return false;
    }
    return fclose(file) == 0;
}

static void check_toolchain_fake_dxc(void) {
    static uint8_t premade[65536];
    static uint8_t out[65536];
    size_t premade_len = 0;
    size_t written = 0;
    DxilDiagnostic diagnostic;
    DxilDiagKind err = DXIL_DIAG_INVALID_IR;
    DxilToolchainConfig config;
    const char *dir = "/tmp/dxil-fake-dxc-test";
    char script_path[256];
    char premade_path[256];
    char cache_path[256];
    const char *script_text =
        "#!/bin/sh\n"
        "while [ $# -gt 0 ]; do\n"
        "  if [ \"$1\" = \"-Fo\" ]; then\n"
        "    shift\n"
        "    cp /tmp/dxil-fake-dxc-test/premade.dxbc \"$1\"\n"
        "  fi\n"
        "  shift\n"
        "done\n";
    snprintf(script_path, sizeof(script_path), "%s/dxc.sh", dir);
    snprintf(premade_path, sizeof(premade_path), "%s/premade.dxbc",
        dir);
    snprintf(cache_path, sizeof(cache_path), "%s/cache", dir);
    CHECK(mkdir(dir, 0755) == 0 || errno == EEXIST);
    CHECK(mkdir(cache_path, 0755) == 0 || errno == EEXIST);
    dxil_diagnostic_clear(&diagnostic);
    CHECK(translate_source(canonical_source, premade,
        sizeof(premade), &premade_len, &diagnostic, &err));
    CHECK(write_bytes_file(premade_path, premade, premade_len));
    CHECK(write_text_file(script_path, script_text));
    CHECK(chmod(script_path, 0755) == 0);
    memset(&config, 0, sizeof(config));
    config.executable = script_path;
    config.discovery = DXIL_DISCOVERY_EXPLICIT_CONFIG;
    dxil_diagnostic_clear(&diagnostic);
    CHECK(dxil_translate_toolchain(canonical_source,
        strlen(canonical_source), out, sizeof(out), &config,
        cache_path, &written, &diagnostic, &err));
    CHECK(written == premade_len);
    CHECK(memcmp(out, premade, premade_len) == 0);
    unlink(script_path);
    unlink(premade_path);
    memset(&config, 0, sizeof(config));
    config.executable = "";
    config.discovery = DXIL_DISCOVERY_EXPLICIT_CONFIG;
    dxil_diagnostic_clear(&diagnostic);
    CHECK(!dxil_translate_toolchain(canonical_source,
        strlen(canonical_source), out, sizeof(out), &config,
        cache_path, &written, &diagnostic, &err));
    CHECK(err == DXIL_DIAG_SHADER_TOOLCHAIN_UNAVAILABLE);
    memset(&config, 0, sizeof(config));
    config.executable = "/nonexistent/dxc-for-doe-test";
    config.discovery = DXIL_DISCOVERY_EXPLICIT_CONFIG;
    dxil_diagnostic_clear(&diagnostic);
    CHECK(!dxil_translate_toolchain(canonical_source,
        strlen(canonical_source), out, sizeof(out), &config,
        cache_path, &written, &diagnostic, &err));
    CHECK(err == DXIL_DIAG_SHADER_TOOLCHAIN_UNAVAILABLE);
    rmdir(cache_path);
    rmdir(dir);
}

int main(void) {
    check_canonical();
    check_storage_and_vertex();
    check_analyze_errors();
    check_toolchain_fake_dxc();
    TEST_SUMMARY("test_dxil_analyze_translate");
}
