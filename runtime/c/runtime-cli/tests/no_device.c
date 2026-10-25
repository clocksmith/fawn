/* SPDX-License-Identifier: MIT
 * Interpose the actual instance boundary: host CLI modes must never call it. */
#include "webgpu.h"
#include <stdlib.h>
WGPUInstance wgpuCreateInstance(const WGPUInstanceDescriptor *descriptor)
{
    (void)descriptor;
    _Exit(99);
}
