/* Relocatable Linux product entry point. execv preserves the owned browser PID
 * and private CDP descriptors; the browser retains its normal sandbox policy. */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    const char *data;
    size_t length;
} ArgumentView;

static int ascii_space(char value) {
    return value == ' ' || value == '\t' || value == '\n' ||
           value == '\r' || value == '\f' || value == '\v';
}

/* Match pinned base/command_line.cc's POSIX trimming and '-'/'--' prefixes
 * for protected switches only. Ordinary argument bytes remain borrowed. */
static ArgumentView argument_view(const char *text) {
    ArgumentView view = {text, strlen(text)};
    while (view.length && ascii_space(*view.data)) { ++view.data; --view.length; }
    while (view.length && ascii_space(view.data[view.length - 1])) --view.length;
    return view;
}

static int view_equal(ArgumentView view, const char *text) {
    size_t length = strlen(text);
    return view.length == length && memcmp(view.data, text, length) == 0;
}

static int protected_switch(ArgumentView view, const char *name, ArgumentView *value) {
    if (view.length < 2 || view.data[0] != '-') return 0;
    size_t prefix = view.data[1] == '-' ? 2 : 1;
    const char *key = view.data + prefix;
    size_t remaining = view.length - prefix;
    const char *equals = memchr(key, '=', remaining);
    size_t key_length = equals ? (size_t)(equals - key) : remaining;
    if (key_length != strlen(name) || memcmp(key, name, key_length) != 0) return 0;
    value->data = equals ? equals + 1 : key + key_length;
    value->length = equals ? remaining - key_length - 1 : 0;
    return 1;
}

static int package_path(char *out, size_t size, const char *root, const char *suffix) {
    int length = snprintf(out, size, "%s/%s", root, suffix);
    return length >= 0 && (size_t)length < size;
}

static int readable_file(const char *path) {
    struct stat metadata;
    return stat(path, &metadata) == 0 && S_ISREG(metadata.st_mode) && access(path, R_OK) == 0;
}

int main(int argc, char **argv) {
    char image[PATH_MAX];
    char browser[PATH_MAX];
    char library[PATH_MAX];
    char compiler[PATH_MAX];
    char library_argument[PATH_MAX + 64];
    char profile_argument[PATH_MAX + 64];
    char **forwarded = NULL;
    int result = 1;
    int dawn = 0;
    int explicit_mode = 0;
    int positional = 0;
    int has_profile = 0;
    size_t count = 0;
    ssize_t length = readlink("/proc/self/exe", image, sizeof(image) - 1);
    if (length <= 0 || (size_t)length >= sizeof(image) - 1) {
        fputs("Fawn: cannot resolve the installed launcher image\n", stderr);
        goto cleanup;
    }
    image[length] = '\0';
    char *separator = strrchr(image, '/');
    if (!separator) goto cleanup;
    *separator = '\0';
    if (!package_path(browser, sizeof(browser), image, "chrome") ||
        !package_path(library, sizeof(library), image, "lib/doe/libwebgpu_doe_c.so") ||
        !package_path(compiler, sizeof(compiler), image, "lib/doe/libdoe_compiler.so")) {
        fputs("Fawn: installed package path is too long\n", stderr);
        goto cleanup;
    }
    for (int index = 1; index < argc; ++index) {
        if (positional) continue;
        ArgumentView view = argument_view(argv[index]);
        ArgumentView value = {NULL, 0};
        if (view_equal(view, "--")) { positional = 1; continue; }
        if (protected_switch(view, "use-webgpu-runtime", &value)) {
            if (explicit_mode || (!view_equal(value, "doe") && !view_equal(value, "dawn"))) {
                fputs("Fawn: select exactly one explicit doe or dawn runtime\n", stderr);
                goto cleanup;
            }
            explicit_mode = 1;
            dawn = view_equal(value, "dawn");
        } else if (protected_switch(view, "doe-webgpu-library-path", &value)) {
            fputs("Fawn: the product launcher uses its bundled C library; use the raw image for diagnostics\n", stderr);
            goto cleanup;
        }
        if (protected_switch(view, "user-data-dir", &value)) {
            if (has_profile++ || !value.length || value.data[0] != '/') {
                fputs("Fawn: select one absolute user-data-dir\n", stderr);
                goto cleanup;
            }
        }
    }
    if (!readable_file(browser) || access(browser, X_OK) != 0) {
        fprintf(stderr, "Fawn: bundled Chromium image is unavailable: %s\n", browser);
        goto cleanup;
    }
    if (!dawn && (!readable_file(library) || !readable_file(compiler))) {
        fputs("Fawn: bundled Doe C runtime/compiler is missing or unreadable; no fallback\n", stderr);
        goto cleanup;
    }
    forwarded = calloc((size_t)argc + 5, sizeof(*forwarded));
    if (!forwarded) { fputs("Fawn: cannot allocate launch arguments\n", stderr); goto cleanup; }
    forwarded[count++] = browser;
    forwarded[count++] = dawn ? "--use-webgpu-runtime=dawn" : "--use-webgpu-runtime=doe";
    if (!dawn) {
        int written = snprintf(library_argument, sizeof(library_argument), "--doe-webgpu-library-path=%s", library);
        if (written < 0 || (size_t)written >= sizeof(library_argument)) goto cleanup;
        forwarded[count++] = library_argument;
    }
    if (!has_profile) {
        const char *base = getenv("XDG_CONFIG_HOME");
        const char *suffix = "";
        if (!base || base[0] != '/') { base = getenv("HOME"); suffix = "/.config"; }
        if (!base || base[0] != '/') {
            fputs("Fawn: cannot resolve a private profile; select an absolute user-data-dir\n", stderr);
            goto cleanup;
        }
        int written = snprintf(profile_argument, sizeof(profile_argument),
            "--user-data-dir=%s%s/fawn/%s", base, suffix, dawn ? "dawn" : "doe-c");
        if (written < 0 || (size_t)written >= sizeof(profile_argument)) {
            fputs("Fawn: private profile path is too long\n", stderr);
            goto cleanup;
        }
        forwarded[count++] = profile_argument;
    }
    positional = 0;
    for (int index = 1; index < argc; ++index) {
        ArgumentView view = argument_view(argv[index]);
        ArgumentView value = {NULL, 0};
        if (!positional && protected_switch(view, "use-webgpu-runtime", &value)) continue;
        if (view_equal(view, "--")) positional = 1;
        forwarded[count++] = argv[index];
    }
    forwarded[count] = NULL;
    execv(browser, forwarded);
    fprintf(stderr, "Fawn: cannot execute Chromium: %s\n", strerror(errno));
cleanup:
    free(forwarded);
    return result;
}
