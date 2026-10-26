/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "dxil_spec.h"

void dxil_part_fourcc_bytes(DxilPartFourcc fourcc, uint8_t out[4]) {
    const char *text = "????";
    switch (fourcc) {
        case DXIL_PART_FOURCC_DXIL: text = "DXIL"; break;
        case DXIL_PART_FOURCC_ISGN: text = "ISGN"; break;
        case DXIL_PART_FOURCC_OSGN: text = "OSGN"; break;
        case DXIL_PART_FOURCC_PSV0: text = "PSV0"; break;
        case DXIL_PART_FOURCC_RDEF: text = "RDEF"; break;
        case DXIL_PART_FOURCC_SHDR: text = "SHDR"; break;
        case DXIL_PART_FOURCC_SFI0: text = "SFI0"; break;
        case DXIL_PART_FOURCC_HASH: text = "HASH"; break;
    }
    out[0] = (uint8_t)text[0];
    out[1] = (uint8_t)text[1];
    out[2] = (uint8_t)text[2];
    out[3] = (uint8_t)text[3];
}
