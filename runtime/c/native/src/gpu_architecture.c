/* SPDX-License-Identifier: MIT
 * PCI vendor/device ID -> GPU architecture family for WGPUAdapterInfo.architecture
 * on Vulkan and D3D12 (owner decision 2026-11-12). Metal names its own family.
 *
 * Rule: list only device IDs whose chip is named by public data, and give each
 * chip the family its vendor publishes for it. An unlisted ID reports "" (the
 * WebGPU unknown value); there are no ID ranges or vendor-wide defaults, since
 * neighbouring IDs belong to different families (0x73F0 is Navi 33, not Navi 23).
 * Family strings are the accurate published family in the lowercase
 * "<family>-<generation>" form WebGPU adapters commonly report. Routing aliases
 * (a consumer treating RDNA 3.5 like RDNA 3) belong to the consumer, not here.
 *
 * Sources (facts only; no other implementation's table or code was consulted):
 * - Device ID -> chip codename: PCI ID Repository, pci.ids 2026.02.12
 *   (https://pci-ids.ucw.cz), vendor 1002 and 8086 entries.
 * - Device ID -> product name: libdrm amdgpu.ids 2.4.131
 *   (https://gitlab.freedesktop.org/mesa/drm/-/blob/main/data/amdgpu.ids).
 * - Product -> architecture: AMD product specification pages (amd.com/en/products):
 *   Radeon RX 6000 and Ryzen 6000 (680M/660M), Ryzen 7000/9000 desktop and 7020
 *   (610M) integrated graphics: RDNA 2; Radeon RX 7000 / PRO W7000 and Ryzen
 *   7040/8040 (780M/760M/740M): RDNA 3; Ryzen AI 300 (890M/880M), Ryzen AI Max
 *   (8060S/8050S) and Ryzen AI 200/300 Krackan (860M/840M): RDNA 3.5; Radeon
 *   RX 9000 / AI PRO R9700: RDNA 4. Intel: 11th Gen Core (Tiger Lake) Iris Xe and
 *   UHD graphics are Xe-LP, "Gen12LP" in Intel's open-source graphics PRMs.
 */
#include "gpu_architecture.h"

#include <stddef.h>

typedef enum gpu_family {
    GPU_FAMILY_RDNA_2,
    GPU_FAMILY_RDNA_3,
    GPU_FAMILY_RDNA_3_5,
    GPU_FAMILY_RDNA_4,
    GPU_FAMILY_GEN_12LP,
} gpu_family;

typedef struct gpu_family_entry {
    uint16_t vendor_id;
    uint16_t device_id;
    gpu_family family;
} gpu_family_entry;

static const gpu_family_entry gpu_families[] = {
    /* AMD RDNA 2 */
    {0x1002, 0x73A5, GPU_FAMILY_RDNA_2}, /* Navi 21: RX 6950 XT */
    {0x1002, 0x73AF, GPU_FAMILY_RDNA_2}, /* Navi 21: RX 6900 XT */
    {0x1002, 0x73BF, GPU_FAMILY_RDNA_2}, /* Navi 21: RX 6800 / 6800 XT / 6900 XT */
    {0x1002, 0x73DF, GPU_FAMILY_RDNA_2}, /* Navi 22: RX 6700 / 6700 XT / 6750 XT */
    {0x1002, 0x73EF, GPU_FAMILY_RDNA_2}, /* Navi 23: RX 6650 XT */
    {0x1002, 0x73FF, GPU_FAMILY_RDNA_2}, /* Navi 23: RX 6600 / 6600 XT */
    {0x1002, 0x7422, GPU_FAMILY_RDNA_2}, /* Navi 24: PRO W6400 */
    {0x1002, 0x7424, GPU_FAMILY_RDNA_2}, /* Navi 24: RX 6300 */
    {0x1002, 0x743F, GPU_FAMILY_RDNA_2}, /* Navi 24: RX 6400 / 6500 XT */
    {0x1002, 0x1681, GPU_FAMILY_RDNA_2}, /* Rembrandt: Radeon 680M / 660M */
    {0x1002, 0x164E, GPU_FAMILY_RDNA_2}, /* Raphael: Ryzen 7000 desktop graphics */
    {0x1002, 0x13C0, GPU_FAMILY_RDNA_2}, /* Granite Ridge: Ryzen 9000 desktop graphics */
    {0x1002, 0x1506, GPU_FAMILY_RDNA_2}, /* Mendocino: Radeon 610M */
    /* AMD RDNA 3 */
    {0x1002, 0x7448, GPU_FAMILY_RDNA_3}, /* Navi 31: PRO W7900 */
    {0x1002, 0x744C, GPU_FAMILY_RDNA_3}, /* Navi 31: RX 7900 XT / XTX / GRE */
    {0x1002, 0x745E, GPU_FAMILY_RDNA_3}, /* Navi 31: PRO W7800 */
    {0x1002, 0x7470, GPU_FAMILY_RDNA_3}, /* Navi 32: PRO W7700 */
    {0x1002, 0x747E, GPU_FAMILY_RDNA_3}, /* Navi 32: RX 7700 XT / 7800 XT */
    {0x1002, 0x73F0, GPU_FAMILY_RDNA_3}, /* Navi 33: RX 7600M XT */
    {0x1002, 0x7480, GPU_FAMILY_RDNA_3}, /* Navi 33: RX 7600 / 7600 XT / PRO W7600 */
    {0x1002, 0x7483, GPU_FAMILY_RDNA_3}, /* Navi 33: RX 7600M / 7600M XT */
    {0x1002, 0x7489, GPU_FAMILY_RDNA_3}, /* Navi 33: PRO W7500 */
    {0x1002, 0x7499, GPU_FAMILY_RDNA_3}, /* Navi 33: RX 7400 / 7300 / PRO W7400 */
    {0x1002, 0x15BF, GPU_FAMILY_RDNA_3}, /* Phoenix1: Radeon 780M / 760M / 740M */
    {0x1002, 0x15C8, GPU_FAMILY_RDNA_3}, /* Phoenix2: Radeon 740M */
    {0x1002, 0x1900, GPU_FAMILY_RDNA_3}, /* HawkPoint1: Radeon 780M */
    {0x1002, 0x1901, GPU_FAMILY_RDNA_3}, /* HawkPoint2: Radeon 740M */
    /* AMD RDNA 3.5 */
    {0x1002, 0x150E, GPU_FAMILY_RDNA_3_5}, /* Strix: Radeon 890M / 880M */
    {0x1002, 0x1586, GPU_FAMILY_RDNA_3_5}, /* Strix Halo: Radeon 8060S / 8050S / 8040S */
    {0x1002, 0x1114, GPU_FAMILY_RDNA_3_5}, /* Krackan: Radeon 860M / 840M */
    /* AMD RDNA 4 */
    {0x1002, 0x7550, GPU_FAMILY_RDNA_4}, /* Navi 48: RX 9070 / 9070 XT / 9070 GRE */
    {0x1002, 0x7551, GPU_FAMILY_RDNA_4}, /* Navi 48: AI PRO R9700 */
    {0x1002, 0x7590, GPU_FAMILY_RDNA_4}, /* Navi 44: RX 9060 XT */
    /* Intel Gen12LP (Xe-LP), Tiger Lake only */
    {0x8086, 0x9A40, GPU_FAMILY_GEN_12LP}, /* Tiger Lake-UP4 GT2: Iris Xe */
    {0x8086, 0x9A49, GPU_FAMILY_GEN_12LP}, /* Tiger Lake-LP GT2: Iris Xe */
    {0x8086, 0x9A60, GPU_FAMILY_GEN_12LP}, /* Tiger Lake-H GT1: UHD */
    {0x8086, 0x9A68, GPU_FAMILY_GEN_12LP}, /* Tiger Lake-H GT1: UHD */
    {0x8086, 0x9A70, GPU_FAMILY_GEN_12LP}, /* Tiger Lake-H GT1: UHD */
    {0x8086, 0x9A78, GPU_FAMILY_GEN_12LP}, /* Tiger Lake-LP GT2: UHD G4 */
};

static const char *gpu_family_name(gpu_family family)
{
    switch (family) {
    case GPU_FAMILY_RDNA_2: return "rdna-2";
    case GPU_FAMILY_RDNA_3: return "rdna-3";
    case GPU_FAMILY_RDNA_3_5: return "rdna-3.5";
    case GPU_FAMILY_RDNA_4: return "rdna-4";
    case GPU_FAMILY_GEN_12LP: return "gen-12lp";
    }
    return "";
}

const char *native_gpu_architecture(uint32_t vendor_id, uint32_t device_id)
{
    for (size_t i = 0; i < sizeof(gpu_families) / sizeof(gpu_families[0]); ++i) {
        const gpu_family_entry *entry = &gpu_families[i];
        if (entry->vendor_id == vendor_id && entry->device_id == device_id)
            return gpu_family_name(entry->family);
    }
    return "";
}
