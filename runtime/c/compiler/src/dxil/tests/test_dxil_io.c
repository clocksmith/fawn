/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
/* Controlled child process tests: this executable is a fake DXC artifact
 * producer, not a shader compiler or a Windows/D3D12 qualification receipt. */
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "dxil_container.h"
#include "dxil_emit.h"
#include "dxil_files.h"
#include "dxil_translate.h"
#include "dxil_validate.h"
#include "test_util.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

static const char hlsl_input[] = "fake-dxc-input\n";

static size_t fixture(uint8_t *out, size_t capacity) {
    const uint8_t bitcode[] = {'B', 'C', 0xc0, 0xde};
    uint8_t program[64];
    size_t program_length = 0u;
    size_t length = 0u;
    if (dxil_write_program_part(DXIL_SHADER_COMPUTE, 6u, 0u,
            bitcode, sizeof(bitcode), program, sizeof(program),
            &program_length) != DXIL_OK) return 0u;
    const DxilPart part = {DXIL_PART_FOURCC_DXIL, program, program_length};
    if (dxil_write_container(&part, 1u, out, capacity, &length) != DXIL_OK) {
        return 0u;
    }
    return length;
}

static int maximum_fixture(FILE *file) {
    static uint8_t bitcode[262076] = {'B', 'C', 0xc0, 0xde};
    static uint8_t program[262100];
    static uint8_t container[262144];
    size_t program_length = 0u;
    size_t length = 0u;
    if (dxil_write_program_part(DXIL_SHADER_COMPUTE, 6u, 0u, bitcode,
            sizeof(bitcode), program, sizeof(program), &program_length) != DXIL_OK) {
        (void)fclose(file);
        return 95;
    }
    const DxilPart part = {DXIL_PART_FOURCC_DXIL, program, program_length};
    if (dxil_write_container(&part, 1u, container, sizeof(container),
            &length) != DXIL_OK || length != sizeof(container) ||
        fwrite(container, 1u, length, file) != length) {
        (void)fclose(file);
        return 96;
    }
    return fclose(file) == 0 ? 0 : 98;
}

static int fake_child(int argc, char **argv) {
    uint8_t bytes[256];
    char input[4096] = {0};
    FILE *file = NULL;
    size_t length = 0u;
    if ((argc != 8 && argc != 9) || strcmp(argv[1], "-T") != 0 ||
        strcmp(argv[3], "-E") != 0 ||
        strcmp(argv[5], "-Fo") != 0) return 91;
    bool native16 = strcmp(argv[4], "native16") == 0;
    if (strcmp(argv[2], native16 ? "cs_6_2" : "cs_6_0") != 0 ||
        argc != (native16 ? 9 : 8) ||
        (native16 && strcmp(argv[8], "-enable-16bit-types") != 0)) return 91;
    file = dxil_files_read(argv[7]);
    if (file == NULL) return 92;
    length = fread(input, 1u, sizeof(input) - 1u, file);
    if (fclose(file) != 0 || length == 0u ||
        (strcmp(input, hlsl_input) != 0 && strstr(input, "numthreads") == NULL)) {
        return 93;
    }
    if (native16 && strstr(input, "float16_t") == NULL) return 93;
    if (strcmp(argv[4], "missing") == 0) return 0;
    file = dxil_files_create(argv[6]);
    if (file == NULL) return 94;
    if (strcmp(argv[4], "maximum") == 0) return maximum_fixture(file);
    length = fixture(bytes, sizeof(bytes));
    if (length == 0u) {
        (void)fclose(file);
        return 95;
    }
    if (strcmp(argv[4], "malformed") == 0) bytes[0] = 0u;
    if (strcmp(argv[4], "oversized") == 0) {
        memset(bytes, 0, sizeof(bytes));
        for (size_t i = 0u; i < 1024u; ++i) {
            if (fwrite(bytes, 1u, sizeof(bytes), file) != sizeof(bytes)) {
                (void)fclose(file);
                return 96;
            }
        }
        length = 1u;
    }
    if (fwrite(bytes, 1u, length, file) != length) {
        (void)fclose(file);
        return 97;
    }
    if (fclose(file) != 0) return 98;
    if (strcmp(argv[4], "noisy") == 0) {
        char noise[16384];
        memset(noise, 'x', sizeof(noise));
        fputs("intentional noisy child failure\n", stderr);
        /* stderr fills before stdout is closed: a sequential pipe reader
         * deadlocks here. Both streams also exceed the capture bound. */
        for (unsigned i = 0u; i < 8u; ++i) {
            if (fwrite(noise, 1u, sizeof(noise), stderr) != sizeof(noise)) return 90;
        }
        if (fflush(stderr) != 0) return 90;
        fputs("stdout must not override stderr\n", stdout);
        for (unsigned i = 0u; i < 8u; ++i) {
            if (fwrite(noise, 1u, sizeof(noise), stdout) != sizeof(noise)) return 90;
        }
        return 7;
    }
    if (strcmp(argv[4], "cleanup") == 0 ||
        strcmp(argv[4], "failure_cleanup") == 0) {
        char extra[DXIL_FILES_PATH_CAP];
        int count = snprintf(extra, sizeof(extra), "%s.extra", argv[6]);
        if (count < 0 || (size_t)count >= sizeof(extra)) return 99;
        file = dxil_files_create(extra);
        if (file == NULL) return 99;
        if (fclose(file) != 0) return 99;
    }
    if (strcmp(argv[4], "failure") == 0 ||
        strcmp(argv[4], "failure_cleanup") == 0) {
        fputs("intentional controlled-child failure\n", stderr);
        return 7;
    }
    return 0;
}

static void check_case(const char *executable, const char *directory,
    const char *mode, size_t capacity, DxilStatus expected,
    const char *detail) {
    uint8_t out[1024];
    uint8_t original[sizeof(out)];
    uint8_t expected_bytes[256];
    size_t written = 731u;
    DxilDiagnostic diagnostic;
    DxilLoweredModule entry = {
        DXIL_STAGE_COMPUTE, mode, {1u, 1u, 1u}, 6u, 0u, false
    };
    const DxilToolchainConfig config = {
        executable, DXIL_DISCOVERY_EXPLICIT_CONFIG
    };
    memset(out, 0xa5, sizeof(out));
    memcpy(original, out, sizeof(out));
    DxilStatus status = dxil_emit_hlsl_toolchain(&entry, hlsl_input,
        sizeof(hlsl_input) - 1u, out, capacity, &config, directory, &written,
        &diagnostic);
    CHECK_STATUS(status, expected);
    if (expected == DXIL_OK) {
        size_t length = fixture(expected_bytes, sizeof(expected_bytes));
        CHECK(written == length);
        CHECK(memcmp(out, expected_bytes, length) == 0);
        CHECK(memcmp(out + length, original + length, sizeof(out) - length) == 0);
    } else {
        CHECK(written == 731u);
        CHECK(memcmp(out, original, sizeof(out)) == 0);
    }
    if (detail != NULL) CHECK(strstr(diagnostic.message, detail) != NULL);
}

static void check_first_error(void) {
    static DxilIrModule invalid_module;
    uint8_t out[16];
    size_t written = 37u;
    DxilDiagnostic diagnostic;
    const DxilToolchainConfig config = {"", DXIL_DISCOVERY_EXPLICIT_CONFIG};
    memset(out, 0x73, sizeof(out));
    memset(&invalid_module, 0, sizeof(invalid_module));
    DxilStatus status = dxil_emit_toolchain(&invalid_module, out, sizeof(out),
        &config, "never-created", &written, &diagnostic);
    CHECK_STATUS(status, DXIL_SHADER_TOOLCHAIN_UNAVAILABLE);
    CHECK(strstr(diagnostic.message, "DXC executable path is empty") != NULL);
    CHECK(written == 37u);
    for (size_t i = 0u; i < sizeof(out); ++i) CHECK(out[i] == 0x73);
}

static void check_maximum(const char *executable, const char *directory) {
    static uint8_t out[262145];
    size_t written = 73u;
    DxilValidationResult validation;
    const DxilToolchainConfig config = {
        executable, DXIL_DISCOVERY_EXPLICIT_CONFIG
    };
    const DxilLoweredModule entry = {
        DXIL_STAGE_COMPUTE, "maximum", {1u, 1u, 1u}, 6u, 0u, false
    };
    memset(out, 0xa5, sizeof(out));
    DxilStatus status = dxil_emit_hlsl_toolchain(&entry, hlsl_input,
        sizeof(hlsl_input) - 1u, out, sizeof(out) - 1u, &config, directory,
        &written, NULL);
    CHECK_STATUS(status, DXIL_OK);
    CHECK(written == sizeof(out) - 1u);
    CHECK(out[sizeof(out) - 1u] == 0xa5);
    dxil_validate(out, written <= sizeof(out) ? written : 0u, &validation);
    CHECK(validation.valid);
}

static void check_source(const char *executable, const char *directory) {
    static const char valid[] = "@compute @workgroup_size(1) fn valid() {}";
    static const char missing[] = "@compute @workgroup_size(1) fn missing() {}";
    static const char invalid[] = "@compute @workgroup_size(1) fn broken( {}";
    static const char native16[] =
        "enable f16; @group(0) @binding(0) var<storage, read_write> value: f16;"
        "@compute @workgroup_size(1) fn native16() { value = 1.0h; }";
    const char *sources[] = {invalid, valid, missing, valid, native16};
    const char *executables[] = {"", "", executable, executable, executable};
    DxilDiagKind error = DXIL_DIAG_INVALID_IR;
    DxilDiagnostic diagnostic;
    for (size_t i = 0u; i < 5u; ++i) {
        uint8_t out[1024];
        uint8_t original[sizeof(out)];
        size_t written = 317u;
        const DxilToolchainConfig config = {
            executables[i], DXIL_DISCOVERY_EXPLICIT_CONFIG
        };
        memset(out, 0xa5, sizeof(out));
        memcpy(original, out, sizeof(out));
        bool success = dxil_translate_toolchain(sources[i], strlen(sources[i]),
            out, sizeof(out), &config, directory, &written, &diagnostic, &error);
        CHECK(success == (i >= 3u));
        if (i < 3u) {
            CHECK(memcmp(out, original, sizeof(out)) == 0);
            CHECK(written == 317u);
        } else {
            uint8_t expected[256];
            size_t length = fixture(expected, sizeof(expected));
            CHECK(written == length);
            CHECK(memcmp(out, expected, length) == 0);
        }
        if (i == 0u) {
            CHECK(error != DXIL_DIAG_SHADER_TOOLCHAIN_UNAVAILABLE);
            CHECK(diagnostic.stage == DXIL_DIAG_STAGE_PARSER);
            CHECK(strstr(diagnostic.message, "DXC executable path is empty") == NULL);
        } else if (i == 1u) {
            CHECK(error == DXIL_DIAG_SHADER_TOOLCHAIN_UNAVAILABLE);
        } else if (i == 2u) {
            CHECK(error == DXIL_DIAG_INVALID_IR);
            CHECK(strstr(diagnostic.message, "did not write") != NULL);
        }
    }
}

#if !defined(_WIN32)
static void check_closed_standard_streams(const char *executable,
    const char *directory) {
    for (unsigned close_input = 0u; close_input < 2u; ++close_input) {
        pid_t child = fork();
        CHECK(child >= 0);
        if (child < 0) return;
        if (child == 0) {
            uint8_t out[1024];
            size_t written = 73u;
            DxilDiagnostic diagnostic;
            const DxilToolchainConfig config = {
                executable, DXIL_DISCOVERY_EXPLICIT_CONFIG
            };
            const DxilLoweredModule entry = {
                DXIL_STAGE_COMPUTE, "noisy", {1u, 1u, 1u}, 6u, 0u, false
            };
            if (close_input != 0u) (void)close(STDIN_FILENO);
            (void)close(STDOUT_FILENO);
            (void)close(STDERR_FILENO);
            memset(out, 0xa5, sizeof(out));
            DxilStatus status = dxil_emit_hlsl_toolchain(&entry, hlsl_input,
                sizeof(hlsl_input) - 1u, out, sizeof(out), &config, directory,
                &written, &diagnostic);
            if (status != DXIL_INVALID_IR || written != 73u ||
                strstr(diagnostic.message, "intentional noisy child failure")
                    == NULL) _exit(73);
            for (size_t i = 0u; i < sizeof(out); ++i) {
                if (out[i] != 0xa5) _exit(74);
            }
            _exit(0);
        }
        int wait_status = 0;
        pid_t waited;
        do { waited = waitpid(child, &wait_status, 0); }
        while (waited < 0 && errno == EINTR);
        CHECK(waited == child);
        CHECK(WIFEXITED(wait_status) && WEXITSTATUS(wait_status) == 0);
    }
}
#endif

static int run_tests(int argc, char **argv) {
    char directory[256];
    char occupied[320];
    char marker[384];
    FILE *file = NULL;
    if (argc > 1) return fake_child(argc, argv);
    check_first_error();
    (void)snprintf(directory, sizeof(directory),
        "dxil io & \xe9\x9b\xaa-%lu", dxil_files_process_id());
    (void)snprintf(occupied, sizeof(occupied), "%s/doe_wgsl_dxil_%lu_0",
        directory, dxil_files_process_id());
    (void)snprintf(marker, sizeof(marker), "%s/untouched", occupied);
    CHECK(dxil_files_mkdir(directory) == 0);
    CHECK(dxil_files_mkdir(occupied) == 0);
    file = dxil_files_create(marker);
    CHECK(file != NULL);
    if (file == NULL) return 1;
    CHECK(fputs("owned by someone else", file) >= 0);
    CHECK(fclose(file) == 0);
    file = dxil_files_create(marker);
    CHECK(file == NULL);
    CHECK(errno == EEXIST);
    if (file != NULL) (void)fclose(file);
    check_case(argv[0], directory, "valid", 1024u, DXIL_OK, NULL);
    check_maximum(argv[0], directory);
    check_case(argv[0], directory, "missing", 1024u, DXIL_INVALID_IR,
        "did not write");
    check_case(argv[0], directory, "malformed", 1024u, DXIL_INVALID_IR,
        "structurally invalid DXIL container");
    check_case(argv[0], directory, "oversized", 1024u, DXIL_OUTPUT_TOO_LARGE,
        "exceeded the 262144-byte contract");
    check_case(argv[0], directory, "failure", 1024u, DXIL_INVALID_IR,
        "intentional controlled-child failure");
    check_case(argv[0], directory, "noisy", 1024u, DXIL_INVALID_IR,
        "intentional noisy child failure");
    check_case(argv[0], directory, "valid", 1u, DXIL_OUTPUT_TOO_LARGE, NULL);
    check_case("definitely-no-such-dxc-executable", directory, "valid", 1024u,
        DXIL_SHADER_TOOLCHAIN_UNAVAILABLE, NULL);
    check_source(argv[0], directory);
#if !defined(_WIN32)
    check_closed_standard_streams(argv[0], directory);
#endif
    /* A child-created extra file makes cleanup fail without synthetic I/O
     * hooks. No output may publish, and an earlier child error must win. */
    for (unsigned i = 0u; i < 2u; ++i) {
        char extra[384];
        char temporary[320];
        check_case(argv[0], directory, i == 0u ? "cleanup" : "failure_cleanup",
            1024u, DXIL_INVALID_IR, i == 0u
                ? "remove DXIL temporary directory"
                : "intentional controlled-child failure");
        (void)snprintf(temporary, sizeof(temporary), "%s/doe_wgsl_dxil_%lu_1",
            directory, dxil_files_process_id());
        (void)snprintf(extra, sizeof(extra), "%s/output.dxil.extra", temporary);
        CHECK(dxil_files_remove(extra) == 0);
        CHECK(dxil_files_rmdir(temporary) == 0);
    }
    file = dxil_files_read(marker);
    CHECK(file != NULL);
    if (file != NULL) {
        char content[32] = {0};
        CHECK(fread(content, 1u, sizeof(content), file) == 21u);
        CHECK(strcmp(content, "owned by someone else") == 0);
        CHECK(fclose(file) == 0);
    }
    CHECK(dxil_files_remove(marker) == 0);
    CHECK(dxil_files_rmdir(occupied) == 0);
    /* Removing the root succeeds only if every owned temporary directory and
     * both artifact names were removed after successes and every failure. */
    CHECK(dxil_files_rmdir(directory) == 0);
#if defined(_WIN32)
    check_case(argv[0], "\xff", "valid", 1024u, DXIL_INVALID_IR, NULL);
#endif
    TEST_SUMMARY("test_dxil_io (controlled child; no DXC qualification)");
}

#if defined(_WIN32)
int wmain(int argc, wchar_t **wide_argv);
int wmain(int argc, wchar_t **wide_argv) {
    char text[9][DXIL_FILES_PATH_CAP];
    char *argv[9];
    if (argc > 9) return 90;
    for (int i = 0; i < argc; ++i) {
        if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide_argv[i], -1,
                text[i], (int)sizeof(text[i]), NULL, NULL) == 0) return 90;
        argv[i] = text[i];
    }
    return run_tests(argc, argv);
}
#else
int main(int argc, char **argv) {
    return run_tests(argc, argv);
}
#endif
