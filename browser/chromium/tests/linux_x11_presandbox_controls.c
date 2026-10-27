/* Focused real X11 metadata connections across a process-local socket barrier.
 * This does not load GL, execute GPU work, or model Chromium's full policy. */
#define _POSIX_C_SOURCE 200809L
#include <X11/Xlib.h>
#include <xcb/xcb.h>
#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>

static unsigned threads(void) {
    DIR *directory = opendir("/proc/self/task");
    assert(directory);
    unsigned count = 0;
    struct dirent *entry;
    while ((entry = readdir(directory))) if (isdigit((unsigned char)entry->d_name[0])) ++count;
    assert(closedir(directory) == 0);
    return count;
}

static void seal_new_connections(void) {
    struct sock_filter instructions[] = {
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_socket, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_connect, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
    };
    struct sock_fprog program = {(unsigned short)(sizeof(instructions) / sizeof(instructions[0])), instructions};
    assert(prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == 0);
    assert(prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &program) == 0);
    assert(prctl(PR_GET_SECCOMP, 0, 0, 0, 0) == SECCOMP_MODE_FILTER);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    const int prepare = strcmp(argv[1], "prepare-before-seal") == 0;
    assert(prepare || strcmp(argv[1], "seal-before-prepare") == 0);
    Display *display = NULL;
    xcb_connection_t *connection = NULL;
    assert(threads() == 1);
    if (prepare) {
        display = XOpenDisplay(NULL);
        assert(display);
        connection = xcb_connect(NULL, NULL);
        assert(connection && !xcb_connection_has_error(connection));
        assert(threads() == 1);
    }
    seal_new_connections();
    errno = 0;
    assert(socket(AF_UNIX, SOCK_STREAM, 0) == -1 && errno == EPERM);
    assert(XOpenDisplay(NULL) == NULL);
    xcb_connection_t *late = xcb_connect(NULL, NULL);
    assert(late && xcb_connection_has_error(late));
    xcb_disconnect(late);
    if (prepare) {
        XNoOp(display);
        XSync(display, False);
        xcb_get_input_focus_cookie_t cookie = xcb_get_input_focus(connection);
        xcb_get_input_focus_reply_t *reply = xcb_get_input_focus_reply(connection, cookie, NULL);
        assert(reply && !xcb_connection_has_error(connection));
        free(reply);
        xcb_disconnect(connection);
        XCloseDisplay(display);
    }
    assert(threads() == 1);
    printf("PASS %s: process-local seccomp blocks new socket/connect; existing Xlib/XCB metadata survives only when prepared first; no GL/GPU\n", argv[1]);
    return 0;
}
