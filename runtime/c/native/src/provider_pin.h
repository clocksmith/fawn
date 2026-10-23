/* SPDX-License-Identifier: MIT */
#ifndef DOE_NATIVE_PROVIDER_PIN_H
#define DOE_NATIVE_PROVIDER_PIN_H
#include <stdbool.h>

/* Before starting a background native obligation, retain this executable image
 * until process exit. Called without device/resource locks. Failure prevents
 * worker/device publication. A main-executable build already has this lifetime.
 * Exactly one loader reference is deliberately retained per provider image;
 * this is code/dependency residency, never a GPU, device or submission lease. */
bool native_provider_pin(void);

#endif
