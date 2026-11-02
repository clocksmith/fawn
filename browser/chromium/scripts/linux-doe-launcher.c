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

/* A named path admits only the same file (device and inode) as the bundled
 * one, like test -ef; a byte-identical copy elsewhere is foreign. */
static int same_file(ArgumentView value, const char *bundled) {
    char path[PATH_MAX];
    struct stat named, own;
    if (!value.length || value.length >= sizeof(path)) return 0;
    memcpy(path, value.data, value.length);
    path[value.length] = '\0';
    return stat(path, &named) == 0 && stat(bundled, &own) == 0 && S_ISREG(named.st_mode) &&
           named.st_dev == own.st_dev && named.st_ino == own.st_ino;
}

static int readable_file(const char *path) {
    struct stat metadata;
    return stat(path, &metadata) == 0 && S_ISREG(metadata.st_mode) && access(path, R_OK) == 0;
}

/* Percent-encode an absolute path as a file URL, keeping RFC 3986 unreserved
 * bytes and '/' literal. */
static int file_url(char *out, size_t size, const char *prefix, const char *path) {
    static const char hex[] = "0123456789ABCDEF";
    size_t used = strlen(prefix);
    if (used >= size) return 0;
    memcpy(out, prefix, used);
    for (const unsigned char *byte = (const unsigned char *)path; *byte; ++byte) {
        int literal = (*byte >= 'A' && *byte <= 'Z') || (*byte >= 'a' && *byte <= 'z') ||
                      (*byte >= '0' && *byte <= '9') || strchr("-._~/", *byte);
        if (used + (literal ? 1 : 3) >= size) return 0;
        if (literal) {
            out[used++] = (char)*byte;
        } else {
            out[used++] = '%';
            out[used++] = hex[*byte >> 4];
            out[used++] = hex[*byte & 15];
        }
    }
    out[used] = '\0';
    return 1;
}

int main(int argc, char **argv) {
    char image[PATH_MAX];
    char browser[PATH_MAX];
    char library[PATH_MAX];
    char compiler[PATH_MAX];
    char library_argument[PATH_MAX + 64];
    char profile_argument[PATH_MAX + 64];
    char start_page[PATH_MAX];
    char start_url[PATH_MAX * 3 + 16];
    char homepage_argument[PATH_MAX * 3 + 32];
    char **forwarded = NULL;
    int result = 1;
    int dawn = 0;
    int explicit_mode = 0;
    int positional = 0;
    int has_profile = 0;
    int library_seen = 0;
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
        !package_path(compiler, sizeof(compiler), image, "lib/doe/libdoe_compiler.so") ||
        !package_path(start_page, sizeof(start_page), image, "fawn/fawn-cycle.html") ||
        !file_url(start_url, sizeof(start_url), "file://", start_page) ||
        !file_url(homepage_argument, sizeof(homepage_argument), "--homepage=file://", start_page)) {
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
            /* A verifier may name the runtime it selects; admit only the bundled
             * file itself, once. Foreign libraries stay with the raw image. */
            if (library_seen++ || !same_file(value, library)) {
                fputs("Fawn: only the bundled Doe C library may be named; use the raw image for diagnostics\n", stderr);
                goto cleanup;
            }
        }
        if (protected_switch(view, "user-data-dir", &value)) {
            if (has_profile++ || !value.length || value.data[0] != '/') {
                fputs("Fawn: select one absolute user-data-dir\n", stderr);
                goto cleanup;
            }
        }
    }
    if (dawn && library_seen) {
        fputs("Fawn: a Doe library path requires --use-webgpu-runtime=doe\n", stderr);
        goto cleanup;
    }
    if (!readable_file(browser) || access(browser, X_OK) != 0) {
        fprintf(stderr, "Fawn: bundled Chromium image is unavailable: %s\n", browser);
        goto cleanup;
    }
    if (!dawn && (!readable_file(library) || !readable_file(compiler))) {
        fputs("Fawn: bundled Doe C runtime/compiler is missing or unreadable; no fallback\n", stderr);
        goto cleanup;
    }
    if (!readable_file(start_page)) {
        fprintf(stderr, "Fawn: bundled start page is unavailable: %s\n", start_page);
        goto cleanup;
    }
    /* Match the macOS product wrapper and Linux lane launcher: Fawn's desktop
     * identity, window class and start page. */
    if (setenv("CHROME_DESKTOP", "fawn.desktop", 1) != 0) {
        fputs("Fawn: cannot select the Fawn desktop entry\n", stderr);
        goto cleanup;
    }
    forwarded = calloc((size_t)argc + 9, sizeof(*forwarded));
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
    /* Mesa may create threads during GL initialization. Start Chromium's
     * existing GPU sandbox before GL, while it can still seal one thread. */
    if (!dawn) forwarded[count++] = "--gpu-sandbox-start-early";
    forwarded[count++] = "--class=fawn";
    forwarded[count++] = homepage_argument;
    forwarded[count++] = "--new-window";
    forwarded[count++] = start_url;
    positional = 0;
    for (int index = 1; index < argc; ++index) {
        ArgumentView view = argument_view(argv[index]);
        ArgumentView value = {NULL, 0};
        if (!positional && protected_switch(view, "use-webgpu-runtime", &value)) continue;
        if (!positional && protected_switch(view, "doe-webgpu-library-path", &value)) continue;
        if (!dawn && !positional && protected_switch(view, "gpu-sandbox-start-early", &value)) continue;
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
