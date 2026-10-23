/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "doe_dropin_policy_private.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

static int fail_lock;
static int fail_unlock;
int __real_pthread_mutex_lock(pthread_mutex_t *mutex);
int __real_pthread_mutex_unlock(pthread_mutex_t *mutex);
int __wrap_pthread_mutex_lock(pthread_mutex_t *mutex);
int __wrap_pthread_mutex_unlock(pthread_mutex_t *mutex);
int __wrap_pthread_mutex_lock(pthread_mutex_t *mutex) {
    if (fail_lock) return EINVAL;
    return __real_pthread_mutex_lock(mutex);
}
int __wrap_pthread_mutex_unlock(pthread_mutex_t *mutex) {
    if (fail_unlock) return EINVAL;
    return __real_pthread_mutex_unlock(mutex);
}
static const DoeDropinPolicyDescriptor descriptor = {
    .abi_version = DOE_DROPIN_POLICY_ABI_VERSION,
    .struct_size = sizeof(DoeDropinPolicyDescriptor),
    .active_tier = DOE_DROPIN_TIER_COMPUTE,
    .config = &doe_dropin_policy_generated_config
};
int main(void) {
    for (int mode = 0; mode < 2; ++mode) {
        pid_t child = fork();
        assert(child >= 0);
        if (child == 0) {
            fail_lock = mode == 0;
            fail_unlock = mode == 1;
            (void)doe_dropin_policy_bind(&descriptor);
            _exit(1);
        }
        int status;
        assert(waitpid(child, &status, 0) == child);
        assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
    }
    puts("drop-in synchronization faults: lock/unlock both terminate without success");
}
