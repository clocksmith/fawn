/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "dxil_container.h"
#include "dxil_mapping.h"
#include "dxil_spec.h"
#include "dxil_toolchain.h"
#include "dxil_validate.h"
#include "test_util.h"

static void check_builtin(const char *name, uint32_t opcode,
    DxilBuiltinArgMode mode, uint32_t mode_value, bool recognized) {
    DxilBuiltinCall call;
    dxil_builtin_call(name, &call);
    CHECK_MSG(call.opcode == opcode, "%s opcode %u, expected %u", name,
        call.opcode, opcode);
    CHECK_MSG(call.arg_mode == mode, "%s arg mode %d, expected %d", name,
        (int)call.arg_mode, (int)mode);
    CHECK_MSG(call.mode_value == mode_value, "%s mode %u, expected %u",
        name, call.mode_value, mode_value);
    CHECK_MSG(call.recognized == recognized, "%s recognized mismatch",
        name);
}

static void check_binary(DxilBinaryOp op, bool is_float, bool is_signed,
    bool is_comparison, uint32_t opcode) {
    bool got_comparison = false;
    uint32_t got_opcode = 0;
    dxil_binary_opcode(op, is_float, is_signed, &got_comparison,
        &got_opcode);
    CHECK_MSG(got_comparison == is_comparison, "binary %d comparison %d",
        (int)op, (int)got_comparison);
    CHECK_MSG(got_opcode == opcode, "binary %d opcode %u, expected %u",
        (int)op, got_opcode, opcode);
}

static void check_resource(DxilAddrSpace addr, DxilAccess access,
    DxilResourceTypeKind kind, bool found, uint32_t class_out,
    uint32_t kind_out) {
    uint32_t got_class = 0;
    uint32_t got_kind = 0;
    const bool got = dxil_classify_resource(addr, access, kind,
        &got_class, &got_kind);
    CHECK_MSG(got == found, "resource %d/%d/%d found %d", (int)addr,
        (int)access, (int)kind, (int)got);
    if (found) {
        CHECK_MSG(got_class == class_out, "resource class %u, expected %u",
            got_class, class_out);
        CHECK_MSG(got_kind == kind_out, "resource kind %u, expected %u",
            got_kind, kind_out);
    }
}

static void write_file(const char *path, const char *content) {
    FILE *file = fopen(path, "w");
    CHECK(file != NULL);
    if (file == NULL) {
        return;
    }
    fputs(content, file);
    fclose(file);
    chmod(path, 0755);
}

static void read_file(const char *path, char *buf, size_t cap) {
    FILE *file = fopen(path, "r");
    size_t n = 0;
    CHECK(file != NULL);
    if (file == NULL) {
        buf[0] = '\0';
        return;
    }
    n = fread(buf, 1, cap - 1, file);
    buf[n] = '\0';
    fclose(file);
}

int main(void) {
    /* Spec spot checks. */
    CHECK(DXIL_BITCODE_MAGIC_U32 == 0x4243C0DEu);
    CHECK(DXIL_BLOCK_MODULE == 8);
    CHECK(DXIL_BLOCK_TYPE == 17);
    CHECK(DXIL_MODULE_FUNCTION == 8);
    CHECK(DXIL_TYPE_STRUCT_ANON == 18);
    CHECK(DXIL_TYPE_STRUCT_NAMED == 20);
    CHECK(DXIL_OP_THREAD_ID == 93);
    CHECK(DXIL_OP_BARRIER == 80);
    CHECK(DXIL_OP_WAVE_ACTIVE_OP == 119);
    CHECK(DXIL_BARRIER_SYNC_THREAD_GROUP == 1);
    CHECK(DXIL_BARRIER_TGSM_FENCE == 8);
    CHECK(DXIL_SHADER_COMPUTE == 5);
    CHECK(DXIL_SHADER_PIXEL == 0);
    CHECK(strcmp(DXIL_TARGET_TRIPLE_CS, "dxil-ms-dx") == 0);
    CHECK(strcmp(DXIL_TARGET_DATALAYOUT,
        "e-m:e-p:32:32-i1:32-i8:8-i16:16-i32:32-i64:64-f16:16-f32:32-f64:64-n8:16:32:64")
        == 0);
    CHECK(DXIL_MD_NUM_THREADS == 4);
    CHECK(DXIL_RESOURCE_CBV == 2);
    CHECK(DXIL_RESOURCE_STRUCTURED_BUFFER == 12);
    CHECK(DXIL_COMPONENT_F32 == 3);
    CHECK(DXIL_SEMANTIC_DISPATCH_THREAD_ID == 12);
    CHECK(DXIL_LINKAGE_INTERNAL == 8);

    /* Stage mappings. */
    CHECK(dxil_stage_shader_kind(DXIL_STAGE_COMPUTE) == 5);
    CHECK(dxil_stage_shader_kind(DXIL_STAGE_VERTEX) == 1);
    CHECK(dxil_stage_shader_kind(DXIL_STAGE_FRAGMENT) == 0);
    CHECK(strcmp(dxil_stage_prefix(DXIL_STAGE_COMPUTE), "cs") == 0);
    CHECK(strcmp(dxil_stage_prefix(DXIL_STAGE_VERTEX), "vs") == 0);
    CHECK(strcmp(dxil_stage_prefix(DXIL_STAGE_FRAGMENT), "ps") == 0);
    CHECK(DXIL_MD_DX_VERSION_MAJOR == 1);
    CHECK(DXIL_MD_DX_VERSION_MINOR == 0);
    CHECK(DXIL_MD_DX_VALVER_MAJOR == 1);
    CHECK(DXIL_MD_DX_VALVER_MINOR == 7);
    CHECK(DXIL_MD_SHADER_MODEL_MAJOR == 6);
    CHECK(DXIL_MD_SHADER_MODEL_MINOR == 0);

    /* Compute parameter builtins. */
    {
        uint32_t opcode = 0;
        uint32_t calls = 0;
        CHECK(dxil_compute_param_opcode(
            DXIL_BUILTIN_GLOBAL_INVOCATION_ID, &opcode, &calls));
        CHECK(opcode == 93 && calls == 3);
        CHECK(dxil_compute_param_opcode(
            DXIL_BUILTIN_LOCAL_INVOCATION_ID, &opcode, &calls));
        CHECK(opcode == 95 && calls == 3);
        CHECK(dxil_compute_param_opcode(DXIL_BUILTIN_WORKGROUP_ID,
            &opcode, &calls));
        CHECK(opcode == 94 && calls == 3);
        CHECK(dxil_compute_param_opcode(
            DXIL_BUILTIN_LOCAL_INVOCATION_INDEX, &opcode, &calls));
        CHECK(opcode == 96 && calls == 1);
        CHECK(!dxil_compute_param_opcode(DXIL_BUILTIN_VERTEX_INDEX,
            &opcode, &calls));
        CHECK(calls == 0);
    }
    CHECK(dxil_vertex_param_loads_input(DXIL_BUILTIN_VERTEX_INDEX));
    CHECK(dxil_vertex_param_loads_input(DXIL_BUILTIN_INSTANCE_INDEX));
    CHECK(!dxil_vertex_param_loads_input(
        DXIL_BUILTIN_GLOBAL_INVOCATION_ID));
    CHECK(dxil_fragment_param_loads_input(true, DXIL_BUILTIN_OTHER));
    CHECK(dxil_fragment_param_loads_input(false, DXIL_BUILTIN_POSITION));
    CHECK(dxil_fragment_param_loads_input(false,
        DXIL_BUILTIN_FRONT_FACING));
    CHECK(!dxil_fragment_param_loads_input(false, DXIL_BUILTIN_OTHER));

    /* Builtin call table. */
    check_builtin("workgroupBarrier", 80, DXIL_BUILTIN_ARG_MODE_CONSTANT,
        9, true);
    check_builtin("storageBarrier", 80, DXIL_BUILTIN_ARG_MODE_CONSTANT,
        3, true);
    check_builtin("textureBarrier", 80, DXIL_BUILTIN_ARG_MODE_CONSTANT,
        3, true);
    check_builtin("subgroupElect", 110, DXIL_BUILTIN_ARG_NONE, 0, true);
    check_builtin("subgroupAll", 114,
        DXIL_BUILTIN_ARG_FIRST_IF_PRESENT, 0, true);
    check_builtin("subgroupAny", 113,
        DXIL_BUILTIN_ARG_FIRST_IF_PRESENT, 0, true);
    check_builtin("subgroupBallot", 116,
        DXIL_BUILTIN_ARG_FIRST_IF_PRESENT, 0, true);
    check_builtin("subgroupAdd", 119,
        DXIL_BUILTIN_ARG_FIRST_PLUS_WAVE_SUM, 0, true);
    check_builtin("unknownBuiltin", 0, DXIL_BUILTIN_ARG_NONE, 0, false);

    /* Unary ops. */
    {
        uint32_t opcode = 0;
        DxilUnaryForm form = DXIL_UNARY_ZERO_MINUS;
        dxil_unary_opcode(DXIL_UNARY_NEG, true, &opcode, &form);
        CHECK(opcode == DXIL_BINOP_FSUB && form == DXIL_UNARY_ZERO_MINUS);
        dxil_unary_opcode(DXIL_UNARY_NEG, false, &opcode, &form);
        CHECK(opcode == DXIL_BINOP_SUB && form == DXIL_UNARY_ZERO_MINUS);
        dxil_unary_opcode(DXIL_UNARY_NOT, false, &opcode, &form);
        CHECK(opcode == DXIL_BINOP_XOR
            && form == DXIL_UNARY_XOR_ALL_ONES);
        dxil_unary_opcode(DXIL_UNARY_BIT_NOT, false, &opcode, &form);
        CHECK(opcode == DXIL_BINOP_XOR
            && form == DXIL_UNARY_XOR_ALL_ONES);
    }

    /* Binary ops and comparisons. */
    check_binary(DXIL_BINARY_ADD, true, false, false, DXIL_BINOP_FADD);
    check_binary(DXIL_BINARY_ADD, false, false, false, DXIL_BINOP_ADD);
    check_binary(DXIL_BINARY_DIV, true, false, false, DXIL_BINOP_FDIV);
    check_binary(DXIL_BINARY_DIV, false, true, false, DXIL_BINOP_SDIV);
    check_binary(DXIL_BINARY_DIV, false, false, false, DXIL_BINOP_UDIV);
    check_binary(DXIL_BINARY_REM, false, true, false, DXIL_BINOP_SREM);
    check_binary(DXIL_BINARY_REM, false, false, false, DXIL_BINOP_UREM);
    check_binary(DXIL_BINARY_SHIFT_RIGHT, false, true, false,
        DXIL_BINOP_ASHR);
    check_binary(DXIL_BINARY_SHIFT_RIGHT, false, false, false,
        DXIL_BINOP_LSHR);
    check_binary(DXIL_BINARY_EQUAL, true, false, true, DXIL_CMP_FCMP_OEQ);
    check_binary(DXIL_BINARY_EQUAL, false, false, true, DXIL_CMP_ICMP_EQ);
    check_binary(DXIL_BINARY_NOT_EQUAL, true, false, true,
        DXIL_CMP_FCMP_ONE);
    check_binary(DXIL_BINARY_LESS, false, true, true, DXIL_CMP_ICMP_SLT);
    check_binary(DXIL_BINARY_LESS, false, false, true, DXIL_CMP_ICMP_ULT);
    check_binary(DXIL_BINARY_LESS_EQUAL, true, false, true,
        DXIL_CMP_FCMP_OLE);
    check_binary(DXIL_BINARY_GREATER, false, true, true,
        DXIL_CMP_ICMP_SGT);
    check_binary(DXIL_BINARY_GREATER_EQUAL, false, false, true,
        DXIL_CMP_ICMP_UGE);
    check_binary(DXIL_BINARY_LOGICAL_AND, false, false, false,
        DXIL_BINOP_AND);

    /* Resource classification. */
    check_resource(DXIL_ADDR_UNIFORM, DXIL_ACCESS_NONE,
        DXIL_RESTYPE_OTHER, true, DXIL_RESOURCE_CBV,
        DXIL_RESOURCE_CBUFFER);
    check_resource(DXIL_ADDR_STORAGE, DXIL_ACCESS_READ,
        DXIL_RESTYPE_STRUCT, true, DXIL_RESOURCE_SRV,
        DXIL_RESOURCE_STRUCTURED_BUFFER);
    check_resource(DXIL_ADDR_STORAGE, DXIL_ACCESS_NONE,
        DXIL_RESTYPE_OTHER, true, DXIL_RESOURCE_UAV,
        DXIL_RESOURCE_RAW_BUFFER);
    check_resource(DXIL_ADDR_STORAGE, DXIL_ACCESS_READ_WRITE,
        DXIL_RESTYPE_ARRAY, true, DXIL_RESOURCE_UAV,
        DXIL_RESOURCE_STRUCTURED_BUFFER);
    check_resource(DXIL_ADDR_HANDLE, DXIL_ACCESS_NONE,
        DXIL_RESTYPE_SAMPLER, true, DXIL_RESOURCE_SAMPLER,
        DXIL_RESOURCE_KIND_SAMPLER);
    check_resource(DXIL_ADDR_HANDLE, DXIL_ACCESS_NONE,
        DXIL_RESTYPE_SAMPLER_COMPARISON, true, DXIL_RESOURCE_SAMPLER,
        DXIL_RESOURCE_KIND_SAMPLER);
    check_resource(DXIL_ADDR_HANDLE, DXIL_ACCESS_NONE,
        DXIL_RESTYPE_TEXTURE_2D_ARRAY, true, DXIL_RESOURCE_SRV,
        DXIL_RESOURCE_TEXTURE_2D);
    check_resource(DXIL_ADDR_HANDLE, DXIL_ACCESS_NONE,
        DXIL_RESTYPE_TEXTURE_MULTISAMPLED_2D, true, DXIL_RESOURCE_SRV,
        DXIL_RESOURCE_TEXTURE_2D_MS);
    check_resource(DXIL_ADDR_HANDLE, DXIL_ACCESS_READ,
        DXIL_RESTYPE_STORAGE_TEXTURE_2D, true, DXIL_RESOURCE_SRV,
        DXIL_RESOURCE_TEXTURE_2D);
    check_resource(DXIL_ADDR_HANDLE, DXIL_ACCESS_READ_WRITE,
        DXIL_RESTYPE_STORAGE_TEXTURE_2D, true, DXIL_RESOURCE_UAV,
        DXIL_RESOURCE_TEXTURE_2D);
    check_resource(DXIL_ADDR_HANDLE, DXIL_ACCESS_NONE,
        DXIL_RESTYPE_OTHER, false, 0, 0);
    check_resource(DXIL_ADDR_OTHER, DXIL_ACCESS_NONE,
        DXIL_RESTYPE_OTHER, false, 0, 0);

    /* Toolchain parsing. */
    CHECK(strcmp(dxil_discovery_label(DXIL_DISCOVERY_EXPLICIT_CONFIG),
        "explicit-config") == 0);
    CHECK(strcmp(dxil_discovery_label(DXIL_DISCOVERY_ENV_PATH),
        "environment-path") == 0);
    CHECK(strcmp(dxil_discovery_label(DXIL_DISCOVERY_ENV_PATH_LOOKUP),
        "environment-PATH") == 0);
    CHECK(strcmp(dxil_discovery_label(
        DXIL_DISCOVERY_IMPLICIT_PATH_LOOKUP), "implicit-PATH") == 0);
    CHECK(strcmp(dxil_default_dxc_executable(), "dxc") == 0);
    {
        char exe[512];
        char err[512];
        DxilDiscovery discovery = DXIL_DISCOVERY_EXPLICIT_CONFIG;
        CHECK_STATUS(dxil_toolchain_from_env_value("PATH", exe,
            sizeof(exe), &discovery, err, sizeof(err)), DXIL_OK);
        CHECK(discovery == DXIL_DISCOVERY_ENV_PATH_LOOKUP);
        CHECK(strcmp(exe, "dxc") == 0);
        CHECK_STATUS(dxil_toolchain_from_env_value("path", exe,
            sizeof(exe), &discovery, err, sizeof(err)), DXIL_OK);
        CHECK(discovery == DXIL_DISCOVERY_ENV_PATH_LOOKUP);
        CHECK_STATUS(dxil_toolchain_from_env_value("PaTh", exe,
            sizeof(exe), &discovery, err, sizeof(err)), DXIL_OK);
        CHECK(discovery == DXIL_DISCOVERY_ENV_PATH_LOOKUP);
        CHECK_STATUS(dxil_toolchain_from_env_value(
            "  /opt/dxc/bin/dxc  \n", exe, sizeof(exe), &discovery, err,
            sizeof(err)), DXIL_OK);
        CHECK(discovery == DXIL_DISCOVERY_ENV_PATH);
        CHECK(strcmp(exe, "/opt/dxc/bin/dxc") == 0);
        CHECK_STATUS(dxil_toolchain_from_env_value("", exe, sizeof(exe),
            &discovery, err, sizeof(err)),
            DXIL_SHADER_TOOLCHAIN_UNAVAILABLE);
        CHECK(strstr(err, "DOE_WGSL_DXC") != NULL);
        CHECK(strstr(err, "PATH") != NULL);
        CHECK_STATUS(dxil_toolchain_from_env_value("   \t ", exe,
            sizeof(exe), &discovery, err, sizeof(err)),
            DXIL_SHADER_TOOLCHAIN_UNAVAILABLE);
        CHECK_STATUS(dxil_toolchain_from_env_value(NULL, exe,
            sizeof(exe), &discovery, err, sizeof(err)), DXIL_OK);
        CHECK(discovery == DXIL_DISCOVERY_IMPLICIT_PATH_LOOKUP);
        CHECK(strcmp(exe, "dxc") == 0);
        CHECK_STATUS(dxil_toolchain_from_env_value("/opt/dxc/bin/dxc",
            exe, 4, &discovery, err, sizeof(err)),
            DXIL_OUTPUT_TOO_LARGE);
    }
    {
        /* Environment-backed parsing, restored afterwards. */
        char exe[512];
        char err[512];
        DxilDiscovery discovery = DXIL_DISCOVERY_EXPLICIT_CONFIG;
        const char *saved = getenv(DXIL_DXC_ENV_VAR);
        char saved_copy[512];
        bool had_saved = saved != NULL;
        if (had_saved) {
            snprintf(saved_copy, sizeof(saved_copy), "%s", saved);
        }
        setenv(DXIL_DXC_ENV_VAR, "PATH", 1);
        CHECK_STATUS(dxil_toolchain_from_env(exe, sizeof(exe),
            &discovery, err, sizeof(err)), DXIL_OK);
        CHECK(discovery == DXIL_DISCOVERY_ENV_PATH_LOOKUP);
        unsetenv(DXIL_DXC_ENV_VAR);
        CHECK_STATUS(dxil_toolchain_from_env(exe, sizeof(exe),
            &discovery, err, sizeof(err)), DXIL_OK);
        CHECK(discovery == DXIL_DISCOVERY_IMPLICIT_PATH_LOOKUP);
        if (had_saved) {
            setenv(DXIL_DXC_ENV_VAR, saved_copy, 1);
        }
    }

    /* Profiles. */
    {
        char profile[32];
        CHECK_STATUS(dxil_toolchain_profile(DXIL_STAGE_COMPUTE, 6, 0,
            profile, sizeof(profile)), DXIL_OK);
        CHECK(strcmp(profile, "cs_6_0") == 0);
        CHECK_STATUS(dxil_toolchain_profile(DXIL_STAGE_VERTEX, 6, 0,
            profile, sizeof(profile)), DXIL_OK);
        CHECK(strcmp(profile, "vs_6_0") == 0);
        CHECK_STATUS(dxil_toolchain_profile(DXIL_STAGE_FRAGMENT, 6, 1,
            profile, sizeof(profile)), DXIL_OK);
        CHECK(strcmp(profile, "ps_6_1") == 0);
        CHECK_STATUS(dxil_toolchain_profile(DXIL_STAGE_COMPUTE, 100000,
            100000, profile, sizeof(profile)), DXIL_INVALID_IR);
    }

    /* DXC argv order. */
    {
        const char *argv[9];
        dxil_dxc_argv("dxc", "cs_6_0", "main", "out.dxil", "in.hlsl",
            argv);
        CHECK(strcmp(argv[0], "dxc") == 0);
        CHECK(strcmp(argv[1], "-T") == 0);
        CHECK(strcmp(argv[2], "cs_6_0") == 0);
        CHECK(strcmp(argv[3], "-E") == 0);
        CHECK(strcmp(argv[4], "main") == 0);
        CHECK(strcmp(argv[5], "-Fo") == 0);
        CHECK(strcmp(argv[6], "out.dxil") == 0);
        CHECK(strcmp(argv[7], "in.hlsl") == 0);
        CHECK(argv[8] == NULL);
    }

    /* Error mapping is the identity over the shared status set. */
    {
        DxilStatus status = DXIL_OK;
        for (status = DXIL_OK;
            status <= DXIL_SHADER_TOOLCHAIN_UNAVAILABLE;
            status = (DxilStatus)(status + 1)) {
            CHECK(dxil_emit_error_to_translate_error(status) == status);
        }
        CHECK(DXIL_TRANSLATE_MAX_OUTPUT == 256u * 1024u);
    }

    /* Validation gate. */
    {
        uint8_t buf[1024];
        size_t size = 0;
        uint8_t part_buf[256];
        size_t part_len = 0;
        uint8_t magic[4];
        DxilPart part;
        char err[1024];
        dxil_llvm_magic_bytes(magic);
        CHECK_STATUS(dxil_write_program_part(DXIL_SHADER_COMPUTE, 6, 0,
            magic, sizeof(magic), part_buf, sizeof(part_buf),
            &part_len), DXIL_OK);
        part.fourcc = DXIL_PART_FOURCC_DXIL;
        part.data = part_buf;
        part.len = part_len;
        CHECK_STATUS(dxil_write_container(&part, 1, buf, sizeof(buf),
            &size), DXIL_OK);
        CHECK_STATUS(dxil_validate_output(buf, size, "native emitter",
            err, sizeof(err)), DXIL_OK);
        buf[0] = 'X';
        CHECK_STATUS(dxil_validate_output(buf, size, "DXC", err,
            sizeof(err)), DXIL_INVALID_IR);
        CHECK(strstr(err, "DXC") != NULL);
        CHECK(strstr(err, "structurally invalid") != NULL);
        CHECK(strstr(err, "magic") != NULL);
    }

    /* DXC runner against fake executables. */
    {
        char dir_template[] = "/tmp/dxil-dxc-test-XXXXXX";
        char *dir = mkdtemp(dir_template);
        char good_path[512];
        char bad_path[512];
        char out_path[512];
        char in_path[512];
        char content[64];
        char err[2048];
        CHECK(dir != NULL);
        if (dir != NULL) {
            snprintf(good_path, sizeof(good_path), "%s/fake-dxc", dir);
            snprintf(bad_path, sizeof(bad_path), "%s/fake-dxc-bad",
                dir);
            snprintf(out_path, sizeof(out_path), "%s/out.dxil", dir);
            snprintf(in_path, sizeof(in_path), "%s/in.hlsl", dir);
            write_file(good_path,
                "#!/bin/sh\n"
                "out=\"\"\n"
                "prev=\"\"\n"
                "for a in \"$@\"; do\n"
                "  if [ \"$prev\" = \"-Fo\" ]; then out=\"$a\"; fi\n"
                "  prev=\"$a\"\n"
                "done\n"
                "printf 'fake-dxil' > \"$out\"\n");
            write_file(bad_path,
                "#!/bin/sh\n"
                "printf 'boom' >&2\n"
                "exit 3\n");
            write_file(in_path, "void main() {}\n");
            CHECK_STATUS(dxil_dxc_run(good_path, "cs_6_0", "main",
                out_path, in_path, DXIL_DISCOVERY_EXPLICIT_CONFIG, err,
                sizeof(err)), DXIL_OK);
            read_file(out_path, content, sizeof(content));
            CHECK(strcmp(content, "fake-dxil") == 0);
            CHECK_STATUS(dxil_dxc_run(bad_path, "cs_6_0", "main",
                out_path, in_path, DXIL_DISCOVERY_EXPLICIT_CONFIG, err,
                sizeof(err)), DXIL_INVALID_IR);
            CHECK(strstr(err, "exit code 3") != NULL);
            CHECK(strstr(err, "boom") != NULL);
            CHECK(strstr(err, "cs_6_0") != NULL);
            CHECK(strstr(err, "main") != NULL);
            CHECK_STATUS(dxil_dxc_run(
                "/tmp/dxil-dxc-test-no-such-executable-xyz", "cs_6_0",
                "main", out_path, in_path,
                DXIL_DISCOVERY_IMPLICIT_PATH_LOOKUP, err, sizeof(err)),
                DXIL_SHADER_TOOLCHAIN_UNAVAILABLE);
            CHECK(strstr(err, "not found") != NULL);
            unlink(good_path);
            unlink(bad_path);
            unlink(out_path);
            unlink(in_path);
            rmdir(dir);
        }
    }

    TEST_SUMMARY("test_dxil_mapping_toolchain");
}
