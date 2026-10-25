/* SPDX-License-Identifier: MIT
 * Windows CRT wide argv -> existing UTF-8 CLI/module entry point.
 * Only the owner main.c is compiled with main=doe_host_main. */
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int doe_host_main(int argc, char **argv);

int wmain(int argc, wchar_t **wide_argv)
{
    if (_setmode(_fileno(stdout), _O_BINARY) < 0 || _setmode(_fileno(stderr), _O_BINARY) < 0) return 2;
    if (argc < 0 || (size_t)argc > SIZE_MAX / sizeof(char *) - 1) return 2;
    char **argv = calloc((size_t)argc + 1, sizeof(*argv));
    if (!argv) return 2;
    int converted = 0;
    for (; converted < argc; converted++) {
        int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide_argv[converted], -1, NULL, 0, NULL, NULL);
        if (!size) break;
        argv[converted] = malloc((size_t)size);
        if (!argv[converted] || !WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide_argv[converted], -1,
                argv[converted], size, NULL, NULL)) break;
    }
    int result = converted == argc ? doe_host_main(argc, argv) : 2;
    if (converted != argc) fputs("doe-c: cannot convert Windows arguments to UTF-8\n", stderr);
    for (int i = 0; i < argc; i++) free(argv[i]);
    free(argv);
    return result;
}
