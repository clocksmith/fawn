/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "tsir.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char usage[] =
    "usage: doe-tsir-bootstrap-oracle --kernel <name> --inputs <path>\n"
    "       [--semantic-tsir <path>] [--realization-tsir <path>]\n";

typedef struct OracleArgs {
    const char *kernel;
    const char *inputs_path;
} OracleArgs;

static bool parse_args(int argc, char **argv, OracleArgs *out) {
    OracleArgs args = {0};
    for (int i = 1; i < argc; i++) {
        const char *option = argv[i];
        if (strcmp(option, "--kernel") != 0 &&
            strcmp(option, "--inputs") != 0 &&
            strcmp(option, "--semantic-tsir") != 0 &&
            strcmp(option, "--realization-tsir") != 0) {
            return false;
        }
        if (++i == argc) {
            return false;
        }
        if (strcmp(option, "--kernel") == 0) {
            args.kernel = argv[i];
        } else if (strcmp(option, "--inputs") == 0) {
            args.inputs_path = argv[i];
        }
        /* The retained oracle accepts these provenance paths but does not
         * read them; semantic construction remains owned by the oracle. */
    }
    if (args.kernel == NULL || args.inputs_path == NULL) {
        return false;
    }
    *out = args;
    return true;
}

static int read_input(const char *path, char **out, size_t *out_len) {
    FILE *file = fopen(path, "rb");
    char *bytes = NULL;
    size_t length = 0;
    int error = 0;
    if (file == NULL) {
        fprintf(stderr, "oracle: cannot open %s: %s\n", path, strerror(errno));
        return 1;
    }
    bytes = malloc((size_t)TSIR_BOOTSTRAP_MAX_INPUT_BYTES + 1u);
    if (bytes == NULL) {
        error = ENOMEM;
    } else {
        length = fread(bytes, 1, (size_t)TSIR_BOOTSTRAP_MAX_INPUT_BYTES + 1u, file);
        if (ferror(file)) {
            error = errno == 0 ? EIO : errno;
        } else if (length > TSIR_BOOTSTRAP_MAX_INPUT_BYTES) {
            error = EFBIG;
        }
    }
    if (fclose(file) != 0 && error == 0) {
        error = errno == 0 ? EIO : errno;
    }
    if (error != 0) {
        fprintf(stderr, "oracle: cannot read %s: %s\n", path, strerror(error));
        free(bytes);
        return 1;
    }
    *out = bytes;
    *out_len = length;
    return 0;
}

int main(int argc, char **argv) {
    OracleArgs args;
    char *input = NULL;
    size_t input_len = 0;
    char *status_json = NULL;
    size_t status_len = 0;
    TsirStatus status = TSIR_OK;
    int result = 0;
    if (!parse_args(argc, argv, &args)) {
        fputs("{\"detail\":\"invalid arguments\",\"referenceHash\":null,"
            "\"status\":\"not_implemented\"}\n", stdout);
        fputs(usage, stderr);
        return 1;
    }
    if (read_input(args.inputs_path, &input, &input_len) != 0) {
        return 1;
    }
    status = tsir_bootstrap_oracle_run(args.kernel, input, input_len,
        &status_json, &status_len);
    free(input);
    if (status != TSIR_OK) {
        fprintf(stderr, "oracle: execution failed: %s\n", tsir_status_name(status));
        free(status_json);
        return 1;
    }
    if (fwrite(status_json, 1, status_len, stdout) != status_len ||
        fflush(stdout) != 0) {
        fputs("oracle: cannot write status output\n", stderr);
        result = 1;
    }
    free(status_json);
    return result;
}
