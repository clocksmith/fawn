/* SPDX-License-Identifier: MIT
 * A lookup that cannot pay for its next row must fail visibly, even when the
 * wanted resource is present. Missing membership alone remains ordinary false. */
#include "../private.h"
#include <assert.h>
#include <string.h>
int main(void) {
    DoeCompilerMetalBindingV12 rows[2] = {{0,1,1,0,0},{1,2,1,0,0}};
    const DoeMetalMetadata plan = {.bindings=rows,.binding_count=2};
    DoeCompilerDiagnostic diagnostic;
    Source source = {.source="",.diagnostic=&diagnostic};
    for (unsigned scenario = 0; scenario < 4; ++scenario) {
        memset(&diagnostic, 0, sizeof(diagnostic));
        MetalEmitter emitter = {.source=&source,.metadata=&plan,
            .handle_work_left=scenario == 0 ? 0u : scenario == 1 ? 1u : 2u};
        bool found = metal_dense_selected_resource(&emitter, scenario == 3 ? 3u : 2u);
        if (scenario < 2) {
            assert(!found && emitter.failed && diagnostic.status == DOE_COMPILER_UNSUPPORTED);
            assert(strstr(diagnostic.message, "bounded traversal work"));
        } else {
            assert(found == (scenario == 2) && !emitter.failed && diagnostic.status == DOE_COMPILER_OK);
        }
        assert(emitter.handle_work_left == 0);
    }
}
