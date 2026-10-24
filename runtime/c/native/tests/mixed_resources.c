/* SPDX-License-Identifier: MIT */
#include "mixed_resource_cases.h"
int main(void)
{
    setbuf(stdout, NULL); /* Preserve selected-adapter output even on first failure. */
    state s = {0};
    consumer_init(&s);
    mixed_resources_pixels(&s);
    consumer_finish(&s);
    printf("PASS sparse group3/binding999 uniform, storage buffer, sampled/storage texture and sampler, exact GPU outputs and early releases %s\n",
        adapter_class_scope());
}
