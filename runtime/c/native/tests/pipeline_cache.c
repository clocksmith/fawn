/* SPDX-License-Identifier: MIT */
#include "consumer.h"
#include "pipeline_cache_cases.h"
int main(void)
{
    state s = {0};
    consumer_init(&s);
    pipeline_cache_admission(&s, false);
    consumer_finish(&s);
    puts("PASS native optional pipeline cache ABI/output validation, disabled Vulkan cache state and argument validation without WebGPU error callbacks; persistence covered separately");
    return 0;
}
