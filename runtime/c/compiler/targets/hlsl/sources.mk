# SPDX-License-Identifier: MIT
# One HLSL source list for private target checks and the public compiler facade.
HLSL_SOURCE_NAMES = text.c types.c resources.c image_types.c image.c numeric.c access.c control.c stage.c graphics.c

HLSL_SOURCE_NAMES += float_pack.c

HLSL_SOURCE_NAMES += subgroup.c
HLSL_SOURCE_NAMES += profile.c
HLSL_SOURCE_NAMES += assignment.c
