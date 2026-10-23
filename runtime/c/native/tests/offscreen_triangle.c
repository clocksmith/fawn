/* SPDX-License-Identifier: MIT */
#include "metal_graphics_cases.h"
int main(void)
{
    state s = {0};
    consumer_init(&s);
    metal_graphics_pixels(&s);
    consumer_finish(&s);
    printf("PASS public WGSL triangle, early release, discard initialization and packed uploads %s\n", adapter_class_scope());
}
