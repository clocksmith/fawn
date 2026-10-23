/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
ssize_t __real_write(int fd, const void *buffer, size_t length);
int __real_close(int fd);
int __real_link(const char *old_path, const char *new_path);
ssize_t __wrap_write(int fd, const void *buffer, size_t length);
int __wrap_close(int fd);
int __wrap_link(const char *old_path, const char *new_path);
static int selected(const char *op, unsigned count) {
    const char *wanted = getenv("M3_FAULT_OPERATION");
    const char *number = getenv("M3_FAULT_CALL");
    return wanted != NULL && number != NULL && strcmp(wanted, op) == 0 &&
        count == (unsigned)strtoul(number, NULL, 10);
}
ssize_t __wrap_write(int fd, const void *buffer, size_t length) {
    static unsigned calls;
    if (fd > 2) {
        ++calls;
        if (selected("write", calls)) { errno = ENOSPC; return -1; }
        if (selected("eintr", calls)) { errno = EINTR; return -1; }
        if (selected("short", calls) && length > 1) {
            return __real_write(fd, buffer, length / 2);
        }
    }
    return __real_write(fd, buffer, length);
}
int __wrap_close(int fd) {
    static unsigned calls;
    int result = __real_close(fd);
    if (fd > 2 && selected("close", ++calls)) { errno = EIO; return -1; }
    return result;
}
int __wrap_link(const char *old_path, const char *new_path) {
    static unsigned calls;
    if (selected("link", ++calls)) { errno = EIO; return -1; }
    return __real_link(old_path, new_path);
}
