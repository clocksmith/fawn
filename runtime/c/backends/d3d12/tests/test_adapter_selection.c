/* SPDX-License-Identifier: MIT */
#include "mock_native.h"
#include <stdio.h>

static void modes(void) {
    for (unsigned mode = 0; mode < 3; ++mode) {
        doe_dx_options options = {0};
        if (mode == 1) {
            options.adapter_ordinal = 7;
            options.allow_software_adapter = true;
        } else if (mode == 2) {
            options.adapter_selection = DOE_DX_SELECT_WARP;
            options.allow_software_adapter = true;
        }
        doe_dx_context *context = NULL;
        assert(doe_dx_context_create(mode == 0 ? NULL : &options, &context, NULL) == DOE_DX_OK);
        assert(mock.opens == 1 && mock.open_options.adapter_ordinal == options.adapter_ordinal);
        assert(mock.open_options.adapter_selection == options.adapter_selection);
        assert(mock.open_options.allow_software_adapter == options.allow_software_adapter);
        doe_dx_caps caps;
        assert(doe_dx_context_caps(context, &caps) == DOE_DX_OK);
        assert(caps.adapter_selection == options.adapter_selection);
        assert(caps.adapter_ordinal == (mode == 2 ? DOE_DX_NO_ADAPTER_ORDINAL : options.adapter_ordinal));
        /* Echoed link-seam metadata only, never a software-adapter observation. */
        assert(caps.software_adapter == (mode == 2));
        assert(doe_dx_context_destroy(context) == DOE_DX_OK);
        mock_reset();
    }
}

static void preflight(void) {
    const doe_dx_options invalid[] = {
        {.adapter_selection=(doe_dx_adapter_selection)99},
        {.adapter_selection=DOE_DX_SELECT_WARP},
        {.adapter_ordinal=1,.allow_software_adapter=true,.adapter_selection=DOE_DX_SELECT_WARP}
    };
    doe_dx_context *sentinel = (doe_dx_context *)(uintptr_t)1;
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        doe_dx_context *context = sentinel;
        doe_dx_error error = {0};
        assert(doe_dx_context_create(&invalid[i], &context, &error) == DOE_DX_INVALID_ARGUMENT);
        assert(context == sentinel && error.status == DOE_DX_INVALID_ARGUMENT && error.operation);
        assert(!mock.opens && !mock.allocation_attempts && !mock.allocations);
        mock_reset();
    }
}

static void failed_open(void) {
    const doe_dx_options options = {.allow_software_adapter=true,.adapter_selection=DOE_DX_SELECT_WARP};
    doe_dx_context *sentinel = (doe_dx_context *)(uintptr_t)1;
    doe_dx_context *context = sentinel;
    doe_dx_error error = {0};
    mock.failure = MOCK_OPEN;
    assert(doe_dx_context_create(&options, &context, &error) == DOE_DX_NATIVE_FAILURE);
    assert(error.status == DOE_DX_NATIVE_FAILURE && error.native_code != 0);
    assert(context == sentinel && mock.opens == 1 && mock.open_options.adapter_selection == DOE_DX_SELECT_WARP);
    assert(!mock.contexts && !mock.allocations); /* No second ordinal attempt. */
    mock_reset();
}

int main(void) {
    modes();
    preflight();
    failed_open();
    puts("D3D12 adapter selection: 3 host groups passed; no SDK/adapter/driver claim");
    return 0;
}
