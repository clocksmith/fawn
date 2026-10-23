/* SPDX-License-Identifier: MIT */
#include "doe_shader.h"
int main() { return doe_shader_retain(nullptr) == DOE_SHADER_INVALID_ARGUMENT ? 0 : 1; }
