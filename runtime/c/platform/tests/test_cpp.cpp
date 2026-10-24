/* SPDX-License-Identifier: MIT */
#include "doe_platform.h"
#include <cstdlib>

int main()
{
    doe_mutex mutex;
    if (doe_mutex_init(&mutex, true).status != DOE_HOST_OK) std::abort();
    if (doe_mutex_lock(&mutex).status != DOE_HOST_OK) std::abort();
    if (doe_mutex_unlock(&mutex).status != DOE_HOST_OK) std::abort();
    if (doe_mutex_destroy(&mutex).status != DOE_HOST_OK) std::abort();
    return 0;
}
