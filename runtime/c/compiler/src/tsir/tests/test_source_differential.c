/* SPDX-License-Identifier: MIT
 * Semantic-only driver: retain frontend rejections instead of invoking planner. */
#include "../tsir_source.h"
#include "../tsir_digest.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    const size_t capacity = (1u << 20) + 1u;
    if (argc != 2) return 2;
    FILE *file = fopen(argv[1], "rb");
    if (file == NULL) return 2;
    char *source = malloc(capacity);
    if (source == NULL) { fclose(file); return 2; }
    size_t length = fread(source, 1u, capacity, file);
    bool failed = ferror(file) != 0;
    if (fclose(file) != 0) failed = true;
    if (failed || length == capacity) { free(source); return 2; }
    TsirSemantic semantic = {0};
    DoeCompilerDiagnostic diagnostic;
    TsirStatus status = tsir_source_lower(source, length,
        "source-differential-v1", &semantic, &diagnostic);
    free(source);
    if (status != TSIR_OK) {
        fprintf(stderr, "%s: %s\n", tsir_status_name(status), diagnostic.message);
        return 1;
    }
    char *canonical = NULL;
    size_t canonical_length = 0;
    status = tsir_canonicalize_semantic(&semantic, &canonical, &canonical_length);
    tsir_frontend_free_semantic(&semantic);
    if (status != TSIR_OK) { free(canonical); return 1; }
    failed = fwrite(canonical, 1u, canonical_length, stdout) != canonical_length;
    if (fflush(stdout) != 0) failed = true;
    free(canonical);
    return failed ? 2 : 0;
}
