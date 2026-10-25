/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "doe_instance.h"

static const WGPUInstanceFeatureName instance_features[] = {
    WGPUInstanceFeatureName_TimedWaitAny,
    WGPUInstanceFeatureName_MultipleDevicesPerAdapter
};

WGPUSupportedInstanceFeatures doe_instance_features(void)
{
    return (WGPUSupportedInstanceFeatures){
        .featureCount = sizeof(instance_features) / sizeof(instance_features[0]),
        .features = instance_features
    };
}

WGPUInstanceLimits doe_instance_limits(void)
{
    return (WGPUInstanceLimits){.nextInChain = NULL, .timedWaitAnyMaxCount = 64};
}

WGPUBool doe_instance_has_feature(WGPUInstanceFeatureName feature)
{
    for (size_t i = 0; i < sizeof(instance_features) / sizeof(instance_features[0]); ++i)
        if (instance_features[i] == feature) return 1;
    return 0;
}

void doe_instance_features_free_members(WGPUSupportedInstanceFeatures features)
{
    (void)features;
}
