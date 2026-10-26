/* SPDX-License-Identifier: MIT
 * Native source differential driver; no DXC process or device is involved. */
#include "../dxil_translate.h"
#include "../dxil_validate.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    const size_t source_capacity = (1u << 20) + 1u;
    const size_t output_capacity = 256u * 1024u;
    if (argc != 2) return 2;
    FILE *file = fopen(argv[1], "rb");
    if (file == NULL) return 2;
    char *source = malloc(source_capacity);
    uint8_t *output = malloc(output_capacity);
    if (source == NULL || output == NULL) {
        free(source); free(output); fclose(file); return 2;
    }
    const size_t length = fread(source, 1u, source_capacity, file);
    bool failed = ferror(file) != 0;
    if (fclose(file) != 0) failed = true;
    if (failed || length == source_capacity) {
        free(source); free(output); return 2;
    }
    DxilDiagnostic diagnostic;
    DxilTranslateStatus status = DXIL_DIAG_INVALID_IR;
    size_t written = 0u;
    const bool ok = dxil_translate_with_diagnostic(source, length,
        output, output_capacity, &written, &diagnostic, &status);
    free(source);
    if (!ok) {
        fprintf(stderr, "%s %s\n", dxil_diagnostic_kind_name(status),
            dxil_diagnostic_stage_name(diagnostic.stage));
        free(output);
        return 1;
    }
    DxilValidationResult validation;
    dxil_validate(output, written, &validation);
    if (!validation.valid) { free(output); return 2; }
    failed = fwrite(output, 1u, written, stdout) != written;
    if (fflush(stdout) != 0) failed = true;
    free(output);
    return failed ? 2 : 0;
}
