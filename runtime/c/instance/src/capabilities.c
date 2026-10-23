/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "doe_instance.h"

WGPUSupportedInstanceFeatures doe_instance_features(void)
{
    return (WGPUSupportedInstanceFeatures){.featureCount = 0, .features = NULL};
}

WGPUInstanceLimits doe_instance_limits(void)
{
    return (WGPUInstanceLimits){.nextInChain = NULL, .timedWaitAnyMaxCount = 0};
}

WGPUBool doe_instance_has_feature(WGPUInstanceFeatureName feature)
{
    (void)feature;
    return 0;
}

void doe_instance_features_free_members(WGPUSupportedInstanceFeatures features)
{
    (void)features;
}
