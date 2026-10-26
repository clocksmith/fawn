/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "tsir_transcendental.h"

#include <math.h>

float tsir_exp_f32(float x) {
    return (float)exp((double)x);
}

float tsir_tanh_f32(float x) {
    return (float)tanh((double)x);
}

float tsir_sqrt_f32(float x) {
    return (float)sqrt((double)x);
}
