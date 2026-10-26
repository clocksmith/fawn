/* SPDX-License-Identifier: MIT
 * Exercise shared decimal formatting through identity and all non-CSL bodies. */
#include "../tsir_format.h"
#include "../tsir_digest.h"
#include "../tsir_emit.h"
#include <assert.h>
#include <errno.h>
#include <fenv.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void consumers(double value, const char *expected) {
    TsirManifestLoweringEntry entry = {.kernel_ref = "k", .backend = "webgpu",
        .frontend_version = "v", .compiler_version = "v",
        .exactness = {.class = TSIR_EXACTNESS_TOLERANCE_BOUNDED, .tolerance_metric = "max_abs", .tolerance_epsilon = value}};
    char *canonical = NULL;
    size_t length = 0;
    assert(tsir_canonicalize_manifest_entry(&entry, &canonical, &length) == TSIR_OK);
    char needle[384];
    int written = snprintf(needle, sizeof(needle), "\"toleranceEpsilon\":%s,", expected);
    assert(written > 0 && (size_t)written < sizeof(needle));
    assert(strstr(canonical, needle) != NULL);
    free(canonical);
    TsirBufferBinding bindings[3] = {
        {.name = "input", .binding = 0, .elem = TSIR_SCALAR_F32},
        {.name = "scale", .binding = 1, .elem = TSIR_SCALAR_F32},
        {.name = "output", .binding = 2, .elem = TSIR_SCALAR_F32, .read_write = true}};
    TsirSemanticBodyBinding roles[3] = {{0, TSIR_ROLE_INPUT}, {1, TSIR_ROLE_SCALE}, {2, TSIR_ROLE_OUTPUT}};
    TsirSemanticFunction function = {.name = "main", .bindings = bindings, .binding_count = 3,
        .body = {.op = TSIR_BODY_RMS_NORM, .binding_roles = roles, .binding_role_count = 3,
            .has_rms_norm = true, .rms_norm = {
                .formula = TSIR_RMSNORM_SUM_SQUARES_MEAN_EPSILON_RSQRT_SCALE,
                .reduction_target = TSIR_RMSNORM_INTERMEDIATE_SCALAR,
                .epsilon = {.source = TSIR_EPSILON_LITERAL_F32, .has_literal_f64 = true, .literal_f64 = value}}}};
    static const TsirKernelBackend backends[] = {TSIR_KERNEL_WEBGPU, TSIR_KERNEL_MSL, TSIR_KERNEL_DXIL, TSIR_KERNEL_SPIR_V};
    for (size_t i = 0; i < sizeof(backends) / sizeof(backends[0]); ++i) {
        TsirBuffer body;
        tsir_buf_init(&body);
        assert(tsir_emit_kernel_body(&function, backends[i], &body) == TSIR_OK);
        written = snprintf(needle, sizeof(needle), "+ %s%s)", expected, i == 0 ? "" : "f");
        assert(written > 0 && (size_t)written < sizeof(needle));
        assert(strstr(body.data, needle) != NULL);
        tsir_buf_free(&body);
    }
}

int main(int argc, char **argv) {
    fenv_t saved;
    assert(fegetenv(&saved) == 0);
    for (int i = 1; i < argc; ++i) {
        char *end;
        uint64_t bits = strtoull(argv[i], &end, 16);
        assert(*end == '\0');
        double value;
        memcpy(&value, &bits, sizeof(value));
        assert(isfinite(value));
        TsirBuffer buffer;
        tsir_buf_init(&buffer);
        /* Caller rounding and raised exceptions must not influence canonical
         * identities or be consumed by formatting's round-trip conversion. */
        assert(fesetround(FE_UPWARD) == 0);
        assert(feraiseexcept(FE_INVALID) == 0);
        int exceptions = fetestexcept(FE_ALL_EXCEPT);
        errno = EDOM;
        tsir_format_f64(&buffer, value);
        assert(errno == EDOM && fegetround() == FE_UPWARD);
        assert(fetestexcept(FE_ALL_EXCEPT) == exceptions);
        assert(buffer.err == TSIR_OK);
        consumers(value, buffer.data);
        puts(buffer.data);
        tsir_buf_free(&buffer);
    }
    static const uint64_t nonfinite[] = {
        UINT64_C(0x7ff0000000000000), UINT64_C(0xfff0000000000000),
        UINT64_C(0x7ff8000000000001), UINT64_C(0x7ff0000000000001)};
    for (size_t i = 0; i < sizeof(nonfinite) / sizeof(nonfinite[0]); ++i) {
        double value;
        memcpy(&value, &nonfinite[i], sizeof(value));
        assert(feclearexcept(FE_ALL_EXCEPT) == 0);
        assert(feraiseexcept(FE_DIVBYZERO) == 0);
        int exceptions = fetestexcept(FE_ALL_EXCEPT);
        TsirBuffer rejected;
        tsir_buf_init(&rejected);
        errno = ERANGE;
        tsir_format_f64(&rejected, value);
        assert(rejected.err == TSIR_ERR_INVALID && rejected.len == 0);
        assert(fetestexcept(FE_ALL_EXCEPT) == exceptions && errno == ERANGE);
        tsir_buf_free(&rejected);
    }
    TsirBuffer buffer;
    tsir_buf_init(&buffer);
    tsir_format_f64(&buffer, INFINITY);
    assert(buffer.err == TSIR_ERR_INVALID && buffer.len == 0);
    tsir_buf_free(&buffer);
    tsir_buf_init(&buffer);
    buffer.err = TSIR_ERR_NOMEM;
    tsir_format_f64(&buffer, 1.0);
    assert(buffer.err == TSIR_ERR_NOMEM && buffer.len == 0);
    tsir_buf_free(&buffer);
    assert(fesetenv(&saved) == 0);
    return 0;
}
