/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

int wmain(int argc, wchar_t **argv);

/* A real child executable tests CRT argument decoding and pipe pressure. */
int wmain(int argc, wchar_t **argv) {
    size_t i = 0;
    if (argc != 8 || wcscmp(argv[1], L"-T") != 0
        || wcscmp(argv[3], L"-E") != 0 || wcscmp(argv[5], L"-Fo") != 0) {
        return 91;
    }
    if (wcscmp(argv[2], L"success") == 0) {
        FILE *input = _wfopen(argv[7], L"rb");
        FILE *output = NULL;
        if (input == NULL || wcscmp(argv[4], L"entry \"quoted\" \u96ea\\") != 0) {
            if (input != NULL) {
                fclose(input);
            }
            return 92;
        }
        fclose(input);
        output = _wfopen(argv[6], L"wb");
        if (output == NULL) {
            return 93;
        }
        fputs("exact arguments", output);
        return fclose(output) == 0 ? 0 : 94;
    }
    if (wcscmp(argv[2], L"stderr") == 0) {
        fputs("stdout must not win", stdout);
        fputs(" \r\ncompiler failed\t\r\n", stderr);
        return 3;
    }
    if (wcscmp(argv[2], L"stdout") == 0) {
        fputs(" stdout fallback \r\n", stdout);
        fputs(" \t\r\n", stderr);
        return 4;
    }
    if (wcscmp(argv[2], L"noisy") == 0) {
        char bytes[4096];
        memset(bytes, 'x', sizeof(bytes));
        fputs("bounded stderr", stderr);
        for (i = 0; i < 512; i++) {
            fwrite(bytes, 1, sizeof(bytes), stderr);
            fwrite(bytes, 1, sizeof(bytes), stdout);
        }
        return 5;
    }
    if (wcscmp(argv[2], L"crash") == 0) {
        ExitProcess(0xc0000005u);
    }
    if (wcscmp(argv[2], L"handles") == 0) {
        HANDLE unrelated = (HANDLE)(uintptr_t)_wcstoui64(argv[4], NULL, 10);
        /* A child-local handle can reuse the parent's numeric value. Only
         * mutation of the parent's actual event proves inheritance. */
        SetEvent(unrelated);
        return 0;
    }
    return 96;
}
