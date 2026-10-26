/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif
#include "dxil_toolchain.h"
#include "dxil_process.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dxil_validate.h"

#if !defined(_WIN32)
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

const char *dxil_discovery_label(DxilDiscovery discovery) {
    switch (discovery) {
        case DXIL_DISCOVERY_EXPLICIT_CONFIG: return "explicit-config";
        case DXIL_DISCOVERY_ENV_PATH: return "environment-path";
        case DXIL_DISCOVERY_ENV_PATH_LOOKUP: return "environment-PATH";
        case DXIL_DISCOVERY_IMPLICIT_PATH_LOOKUP: return "implicit-PATH";
    }
    return "explicit-config";
}

const char *dxil_default_dxc_executable(void) {
#if defined(_WIN32)
    return "dxc.exe";
#else
    return "dxc";
#endif
}

static void write_message(char *errbuf, size_t errcap, const char *message) {
    if (errbuf == NULL || errcap == 0) {
        return;
    }
    snprintf(errbuf, errcap, "%s", message);
}

static bool is_trim_char(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static bool equals_path_sentinel(const char *text, size_t len) {
    static const char sentinel[] = "PATH";
    size_t i = 0;
    if (len != 4) {
        return false;
    }
    for (i = 0; i < 4; i++) {
        if (toupper((unsigned char)text[i]) != sentinel[i]) {
            return false;
        }
    }
    return true;
}

DxilStatus dxil_toolchain_from_env_value(const char *raw_value,
    char *exe_buf, size_t exe_cap, DxilDiscovery *discovery,
    char *errbuf, size_t errcap) {
    const char *start = NULL;
    const char *end = NULL;
    size_t trimmed_len = 0;
    if (raw_value == NULL) {
        if (exe_cap < strlen(dxil_default_dxc_executable()) + 1) {
            return DXIL_OUTPUT_TOO_LARGE;
        }
        strcpy(exe_buf, dxil_default_dxc_executable());
        *discovery = DXIL_DISCOVERY_IMPLICIT_PATH_LOOKUP;
        return DXIL_OK;
    }
    start = raw_value;
    while (*start != '\0' && is_trim_char(*start)) {
        start++;
    }
    end = raw_value + strlen(raw_value);
    while (end > start && is_trim_char(end[-1])) {
        end--;
    }
    trimmed_len = (size_t)(end - start);
    if (trimmed_len == 0) {
        char message[256];
        snprintf(message, sizeof(message),
            "%s is set but empty; set it to a DXC path or `%s` to opt "
            "into PATH lookup explicitly",
            DXIL_DXC_ENV_VAR, DXIL_DXC_PATH_SENTINEL);
        write_message(errbuf, errcap, message);
        return DXIL_SHADER_TOOLCHAIN_UNAVAILABLE;
    }
    if (equals_path_sentinel(start, trimmed_len)) {
        if (exe_cap < strlen(dxil_default_dxc_executable()) + 1) {
            return DXIL_OUTPUT_TOO_LARGE;
        }
        strcpy(exe_buf, dxil_default_dxc_executable());
        *discovery = DXIL_DISCOVERY_ENV_PATH_LOOKUP;
        return DXIL_OK;
    }
    if (exe_cap < trimmed_len + 1) {
        return DXIL_OUTPUT_TOO_LARGE;
    }
    memcpy(exe_buf, start, trimmed_len);
    exe_buf[trimmed_len] = '\0';
    *discovery = DXIL_DISCOVERY_ENV_PATH;
    return DXIL_OK;
}

DxilStatus dxil_toolchain_from_env(char *exe_buf, size_t exe_cap,
    DxilDiscovery *discovery, char *errbuf, size_t errcap) {
    const char *value = getenv(DXIL_DXC_ENV_VAR);
    if (value == NULL) {
        return dxil_toolchain_from_env_value(NULL, exe_buf, exe_cap,
            discovery, errbuf, errcap);
    }
    return dxil_toolchain_from_env_value(value, exe_buf, exe_cap,
        discovery, errbuf, errcap);
}

DxilStatus dxil_toolchain_profile(DxilStage stage, unsigned major,
    unsigned minor, char *buf, size_t buf_cap) {
    int needed = 0;
    needed = snprintf(buf, buf_cap, "%s_%u_%u", dxil_stage_prefix(stage),
        major, minor);
    if (needed < 0 || (size_t)needed >= buf_cap) {
        return DXIL_INVALID_IR;
    }
    /* Zig's 16-byte buffer makes any longer profile InvalidIr; enforce
     * the same bound even when the caller offers a larger buffer. */
    if ((size_t)needed >= DXIL_DXC_PROFILE_CAP) {
        return DXIL_INVALID_IR;
    }
    return DXIL_OK;
}

void dxil_dxc_argv(const char *executable, const char *profile,
    const char *entry_name, const char *output_path,
    const char *input_path, const char *argv[9]) {
    argv[0] = executable;
    argv[1] = "-T";
    argv[2] = profile;
    argv[3] = "-E";
    argv[4] = entry_name;
    argv[5] = "-Fo";
    argv[6] = output_path;
    argv[7] = input_path;
    argv[8] = NULL;
}

void dxil_dxc_argv_options(const char *executable, const char *profile,
    const char *entry_name, const char *output_path, const char *input_path,
    bool native16, const char *argv[10]) {
    dxil_dxc_argv(executable, profile, entry_name, output_path, input_path, argv);
    if (native16) {
        argv[8] = "-enable-16bit-types";
        argv[9] = NULL;
    }
}

DxilStatus dxil_dxc_run(const char *executable, const char *profile,
    const char *entry_name, const char *output_path,
    const char *input_path, DxilDiscovery discovery,
    char *errbuf, size_t errcap) {
    return dxil_dxc_run_options(executable, profile, entry_name, output_path,
        input_path, discovery, false, errbuf, errcap);
}

DxilStatus dxil_validate_output(const uint8_t *bytes, size_t len,
    const char *producer, char *errbuf, size_t errcap) {
    DxilValidationResult result;
    char message[1024];
    dxil_validate(bytes, len, &result);
    if (result.valid) {
        return DXIL_OK;
    }
    snprintf(message, sizeof(message),
        "%s produced a structurally invalid DXIL container: %s",
        producer,
        result.error_message != NULL ? result.error_message
            : "unknown validation failure");
    write_message(errbuf, errcap, message);
    return DXIL_INVALID_IR;
}

DxilStatus dxil_emit_error_to_translate_error(DxilStatus status) {
    switch (status) {
        case DXIL_OK: return DXIL_OK;
        case DXIL_OUT_OF_MEMORY: return DXIL_OUT_OF_MEMORY;
        case DXIL_OUTPUT_TOO_LARGE: return DXIL_OUTPUT_TOO_LARGE;
        case DXIL_UNSUPPORTED_CONSTRUCT: return DXIL_UNSUPPORTED_CONSTRUCT;
        case DXIL_INVALID_IR: return DXIL_INVALID_IR;
        case DXIL_UNSUPPORTED_BUILTIN: return DXIL_UNSUPPORTED_BUILTIN;
        case DXIL_SHADER_TOOLCHAIN_UNAVAILABLE:
            return DXIL_SHADER_TOOLCHAIN_UNAVAILABLE;
    }
    return DXIL_INVALID_IR;
}

void dxil_dxc_missing_message(DxilDiscovery discovery,
    const char *executable, char *out, size_t out_cap) {
    switch (discovery) {
        case DXIL_DISCOVERY_EXPLICIT_CONFIG:
            snprintf(out, out_cap,
                "DXC executable from explicit DXIL toolchain config was "
                "not found at `%s`; pass a valid ToolchainConfig path "
                "or set %s to a valid DXC path",
                executable, DXIL_DXC_ENV_VAR);
            return;
        case DXIL_DISCOVERY_ENV_PATH:
            snprintf(out, out_cap,
                "%s points to `%s`, but that DXC executable was not "
                "found; fix %s or use `%s` to opt into PATH lookup",
                DXIL_DXC_ENV_VAR, executable, DXIL_DXC_ENV_VAR,
                DXIL_DXC_PATH_SENTINEL);
            return;
        case DXIL_DISCOVERY_ENV_PATH_LOOKUP:
            snprintf(out, out_cap,
                "%s=%s requested explicit PATH lookup, but `%s` was not "
                "found on PATH",
                DXIL_DXC_ENV_VAR, DXIL_DXC_PATH_SENTINEL, executable);
            return;
        case DXIL_DISCOVERY_IMPLICIT_PATH_LOOKUP:
            snprintf(out, out_cap,
                "DXC was not found on PATH (`%s`); set %s to an "
                "absolute/workspace-relative DXC path or `%s` to make "
                "PATH lookup explicit",
                executable, DXIL_DXC_ENV_VAR, DXIL_DXC_PATH_SENTINEL);
            return;
    }
    snprintf(out, out_cap, "DXC executable `%s` was not found", executable);
}

#if !defined(_WIN32)

static void close_pipe(int descriptors[2]) {
    for (size_t i = 0u; i < 2u; ++i) {
        if (descriptors[i] >= 0) (void)close(descriptors[i]);
        descriptors[i] = -1;
    }
}

/* Both pipes must be drained concurrently: waiting for stdout EOF while a
 * child fills stderr deadlocks even if each retained stream is bounded. */
static int drain_output(int output, int errors, char *output_bytes,
    size_t *output_length, char *error_bytes, size_t *error_length) {
    struct pollfd streams[2] = {{output, POLLIN, 0}, {errors, POLLIN, 0}};
    char *bytes[2] = {output_bytes, error_bytes};
    size_t *lengths[2] = {output_length, error_length};
    int failure = 0;
    while (streams[0].fd >= 0 || streams[1].fd >= 0) {
        if (poll(streams, 2u, -1) < 0) {
            if (errno == EINTR) continue;
            failure = errno;
            break;
        }
        for (size_t i = 0u; i < 2u; ++i) {
            if (streams[i].fd < 0 || streams[i].revents == 0) continue;
            if ((streams[i].revents & POLLNVAL) != 0) { failure = EBADF; break; }
            if ((streams[i].revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
                char chunk[4096];
                const ssize_t received = read(streams[i].fd, chunk, sizeof(chunk));
                if (received < 0) {
                    if (errno == EINTR) continue;
                    failure = errno;
                    break;
                }
                if (received == 0) {
                    (void)close(streams[i].fd);
                    streams[i].fd = -1;
                } else {
                    size_t keep = (size_t)received;
                    if (keep > DXIL_DXC_MAX_OUTPUT_BYTES - *lengths[i])
                        keep = DXIL_DXC_MAX_OUTPUT_BYTES - *lengths[i];
                    memcpy(bytes[i] + *lengths[i], chunk, keep);
                    *lengths[i] += keep;
                }
            }
        }
        if (failure != 0) break;
    }
    for (size_t i = 0u; i < 2u; ++i) {
        if (streams[i].fd >= 0) (void)close(streams[i].fd);
        bytes[i][*lengths[i]] = '\0';
    }
    return failure;
}

/* Trims spaces, tabs, CR, and LF from both ends, in place. */
static void trim_in_place(char *text) {
    size_t len = strlen(text);
    size_t start = 0;
    size_t end = len;
    while (start < len && is_trim_char(text[start])) {
        start++;
    }
    while (end > start && is_trim_char(text[end - 1])) {
        end--;
    }
    if (start > 0) {
        memmove(text, text + start, end - start);
    }
    text[end - start] = '\0';
}

DxilStatus dxil_dxc_run_options(const char *executable, const char *profile,
    const char *entry_name, const char *output_path,
    const char *input_path, DxilDiscovery discovery, bool native16,
    char *errbuf, size_t errcap) {
    int out_pipe[2] = { -1, -1 };
    int err_pipe[2] = { -1, -1 };
    int errno_pipe[2] = { -1, -1 };
    pid_t pid = -1;
    const char *argv[10];
    char message[2048];
    char detail_buf[DXIL_DXC_MAX_OUTPUT_BYTES + 1];
    char stderr_buf[DXIL_DXC_MAX_OUTPUT_BYTES + 1];
    size_t detail_len = 0;
    size_t stderr_len = 0;
    int status_value = 0;

    dxil_dxc_argv_options(executable, profile, entry_name, output_path,
        input_path, native16, argv);
    if (pipe(out_pipe) != 0 || pipe(err_pipe) != 0
        || pipe(errno_pipe) != 0) {
        const int saved_errno = errno;
        close_pipe(out_pipe);
        close_pipe(err_pipe);
        close_pipe(errno_pipe);
        snprintf(message, sizeof(message),
            "failed to start DXC via %s `%s`: %s",
            dxil_discovery_label(discovery), executable,
            strerror(saved_errno));
        write_message(errbuf, errcap, message);
        return DXIL_SHADER_TOOLCHAIN_UNAVAILABLE;
    }
    /* The errno pipe reports exec failure without ambiguity: the write
     * end is close-on-exec, so a successful exec closes it and the
     * parent reads EOF, while a failed exec delivers errno. */
    if (fcntl(errno_pipe[1], F_SETFD, FD_CLOEXEC) != 0) {
        const int saved_errno = errno;
        close_pipe(out_pipe);
        close_pipe(err_pipe);
        close_pipe(errno_pipe);
        snprintf(message, sizeof(message), "failed to configure DXC process pipe: %s",
            strerror(saved_errno));
        write_message(errbuf, errcap, message);
        return DXIL_SHADER_TOOLCHAIN_UNAVAILABLE;
    }
    pid = fork();
    if (pid < 0) {
        const int saved_errno = errno;
        close(out_pipe[0]);
        close(out_pipe[1]);
        close(err_pipe[0]);
        close(err_pipe[1]);
        close(errno_pipe[0]);
        close(errno_pipe[1]);
        snprintf(message, sizeof(message),
            "failed to start DXC via %s `%s`: %s",
            dxil_discovery_label(discovery), executable,
            strerror(saved_errno));
        write_message(errbuf, errcap, message);
        return DXIL_SHADER_TOOLCHAIN_UNAVAILABLE;
    }
    if (pid == 0) {
        /* Child: wire stdout/stderr to the pipes, then exec. */
        int child_errno = 0;
        close(out_pipe[0]);
        close(err_pipe[0]);
        close(errno_pipe[0]);
        if (dup2(out_pipe[1], STDOUT_FILENO) < 0 ||
            dup2(err_pipe[1], STDERR_FILENO) < 0) child_errno = errno;
        if (out_pipe[1] != STDOUT_FILENO && out_pipe[1] != STDERR_FILENO)
            close(out_pipe[1]);
        if (err_pipe[1] != STDOUT_FILENO && err_pipe[1] != STDERR_FILENO)
            close(err_pipe[1]);
        if (child_errno == 0) {
            execvp(executable, (char *const *)argv);
            child_errno = errno;
        }
        ssize_t sent;
        do { sent = write(errno_pipe[1], &child_errno, sizeof(child_errno)); }
        while (sent < 0 && errno == EINTR);
        _exit(127);
    }
    close(out_pipe[1]);
    close(err_pipe[1]);
    close(errno_pipe[1]);
    {
        const int drain_error = drain_output(out_pipe[0], err_pipe[0],
            detail_buf, &detail_len, stderr_buf, &stderr_len);
        if (drain_error != 0) {
            (void)kill(pid, SIGKILL);
            (void)close(errno_pipe[0]);
            while (waitpid(pid, &status_value, 0) < 0 && errno == EINTR) {}
            snprintf(message, sizeof(message), "failed to read DXC output: %s",
                strerror(drain_error));
            write_message(errbuf, errcap, message);
            return DXIL_INVALID_IR;
        }
        /* Exec failure is reported on the errno pipe. */
        {
            int child_errno = 0;
            ssize_t got = 0;
            do {
                got = read(errno_pipe[0], &child_errno, sizeof(child_errno));
            } while (got < 0 && errno == EINTR);
            const int read_error = errno;
            close(errno_pipe[0]);
            if (got != 0 && got != (ssize_t)sizeof(child_errno)) {
                (void)kill(pid, SIGKILL);
                while (waitpid(pid, &status_value, 0) < 0 && errno == EINTR) {}
                snprintf(message, sizeof(message), "failed to read DXC process status: %s",
                    strerror(got < 0 ? read_error : EIO));
                write_message(errbuf, errcap, message);
                return DXIL_INVALID_IR;
            }
            if (got == (ssize_t)sizeof(child_errno)) {
                /* Reap the child before reporting. */
                while (waitpid(pid, &status_value, 0) < 0
                    && errno == EINTR) {
                }
                if (child_errno == ENOENT) {
                    dxil_dxc_missing_message(discovery, executable,
                        message, sizeof(message));
                    write_message(errbuf, errcap, message);
                    return DXIL_SHADER_TOOLCHAIN_UNAVAILABLE;
                }
                snprintf(message, sizeof(message),
                    "failed to start DXC via %s `%s`: %s",
                    dxil_discovery_label(discovery), executable,
                    strerror(child_errno));
                write_message(errbuf, errcap, message);
                return DXIL_SHADER_TOOLCHAIN_UNAVAILABLE;
            }
        }
        pid_t waited;
        do { waited = waitpid(pid, &status_value, 0); } while (waited < 0 && errno == EINTR);
        if (waited < 0) {
            snprintf(message, sizeof(message), "failed to wait for DXC: %s", strerror(errno));
            write_message(errbuf, errcap, message);
            return DXIL_INVALID_IR;
        }
    }
    if (WIFEXITED(status_value)) {
        const int code = WEXITSTATUS(status_value);
        if (code != 0) {
            /* Zig's diagnosticOutput: trimmed stderr, else trimmed
             * stdout. Only a bounded head of the detail goes into the
             * message, so the message buffer cannot overflow. */
            char *detail = stderr_buf;
            char detail_head[512];
            size_t detail_head_len = 0;
            size_t k = 0;
            trim_in_place(stderr_buf);
            trim_in_place(detail_buf);
            if (stderr_buf[0] == '\0') {
                detail = detail_buf;
            }
            detail_head_len = strlen(detail) < sizeof(detail_head) - 1
                ? strlen(detail) : sizeof(detail_head) - 1;
            for (k = 0; k < detail_head_len; k++) {
                detail_head[k] = detail[k];
            }
            detail_head[detail_head_len] = '\0';
            if (detail_head[0] == '\0') {
                snprintf(message, sizeof(message),
                    "DXC failed via %s `%s` with exit code %d for "
                    "profile `%s` and entry `%s`",
                    dxil_discovery_label(discovery), executable, code,
                    profile, entry_name);
            } else {
                snprintf(message, sizeof(message),
                    "DXC failed via %s `%s` with exit code %d for "
                    "profile `%s` and entry `%s`: %s",
                    dxil_discovery_label(discovery), executable, code,
                    profile, entry_name, detail_head);
            }
            write_message(errbuf, errcap, message);
            return DXIL_INVALID_IR;
        }
        return DXIL_OK;
    }
    snprintf(message, sizeof(message),
        "DXC terminated unexpectedly via %s `%s` for profile `%s` and "
        "entry `%s`",
        dxil_discovery_label(discovery), executable, profile,
        entry_name);
    write_message(errbuf, errcap, message);
    return DXIL_INVALID_IR;
}

#endif /* _WIN32 */
