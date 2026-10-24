/* SPDX-License-Identifier: MIT */
#ifndef DOE_NATIVE_PROVIDER_PIN_H
#define DOE_NATIVE_PROVIDER_PIN_H
#include <stdbool.h>

/* Before starting a background native obligation, retain this executable image
 * until process exit. Called without device/resource locks. Failure prevents
 * worker/device publication. A main-executable build already has this lifetime.
 * POSIX deliberately retains one loader reference per non-main provider image.
 * Win32 uses one InitOnce/GetModuleHandleEx FROM_ADDRESS|PIN operation and never
 * substitutes a normal dynamic-library reference. Both preserve caller errno;
 * Win32 also preserves GetLastError. This is process-code/dependency residency,
 * never a GPU, device or submission lease. */
bool native_provider_pin(void);

#endif
