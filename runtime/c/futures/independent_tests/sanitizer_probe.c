/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include <pthread.h>
#include <stdlib.h>
static void *worker(void *p) { *(int *)p = 42; return NULL; }
int main(void) {
    int *p = malloc(sizeof(*p)); if (!p) return 2;
    pthread_t thread; if (pthread_create(&thread, NULL, worker, p)) return 3;
    if (pthread_join(thread, NULL)) return 4;
    int status = *p == 42 ? 0 : 5; free(p); return status;
}
