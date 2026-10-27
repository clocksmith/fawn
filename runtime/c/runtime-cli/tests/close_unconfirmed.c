/* SPDX-License-Identifier: MIT
 * Explicit close-failure injection: no claim that device loss was observed. */
#include "webgpu.h"
WGPUFuture wgpuDeviceGetLostFuture(WGPUDevice device)
{
    (void)device;
    return (WGPUFuture){0};
}
