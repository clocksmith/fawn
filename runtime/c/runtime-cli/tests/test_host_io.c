/* SPDX-License-Identifier: MIT */
#include "host_io.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    for (unsigned count = 0; count < 3; count++) {
        doe_host_output output;
        FILE *stream = doe_host_output_open(&output);
        assert(stream);
        size_t expected_size = count == 0 ? 0 : count == 1 ? 256 : 256 * 1024;
        for (size_t i = 0; i < expected_size; i++) assert(fputc((int)(i & 255), stream) != EOF);
        assert(fflush(stream) == 0);
        char *bytes = NULL;
        size_t size = 7;
        assert(doe_host_output_close(&output, &bytes, &size) && size == expected_size && bytes[size] == 0);
        for (size_t i = 0; i < size; i++) assert((unsigned char)bytes[i] == (i & 255));
        free(bytes);
        doe_host_output_discard(&output);
    }
    doe_host_output output;
    assert(doe_host_output_open(&output));
    assert(fputs("discard this private output", output.file) >= 0);
    doe_host_output_discard(&output);
    char sentinel = 's';
    char *bytes = &sentinel;
    size_t size = 13;
    assert(!doe_host_output_close(&output, &bytes, &size) && bytes == &sentinel && size == 13);
    /* The real stdio write boundary fails; no partial bytes may be published. */
    output.file = fopen(__FILE__, "rb");
    assert(output.file && fputc('x', output.file) == EOF);
    assert(!doe_host_output_close(&output, &bytes, &size) && bytes == &sentinel && size == 13);
    char *copy = doe_host_strdup("Escaped\\Path\nwith bytes");
    assert(copy && !strcmp(copy, "Escaped\\Path\nwith bytes"));
    free(copy);
    assert(!doe_host_casecmp("aBc-123", "AbC-123") && doe_host_casecmp("a", "b") < 0);
    assert(doe_host_casecmp("b", "a") > 0 && doe_host_casecmp("", "x") < 0);
    assert(doe_host_regular_file(__FILE__));
    uint64_t before = doe_host_monotonic_ns();
    assert(before && doe_host_realtime_ns());
    doe_host_pause_ns(1000000);
    assert(doe_host_monotonic_ns() >= before);
    puts("PASS host output byte ownership, binary bytes, discard, failed publication, ASCII strings and actual clocks");
    return 0;
}
