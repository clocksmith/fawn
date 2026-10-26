/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "doe_compiler_host.h"

#include <cstdio>
#include <cstring>
#include <type_traits>

static_assert(std::is_standard_layout<DoeCompilerHostBuffer>::value,
    "host result must remain C-compatible");
static_assert(std::is_standard_layout<DoeCompilerHostDiagnostic>::value,
    "host diagnostic must remain C-compatible");

int main() {
    DoeCompilerHostBuffer output{};
    DoeCompilerHostDiagnostic diagnostic{};
    const auto result = doe_compiler_host_tsir_oracle(
        DOE_COMPILER_HOST_ABI_VERSION, "gather", "{}", 2, &output,
        sizeof(output), &diagnostic, sizeof(diagnostic));
    if (result != DOE_COMPILER_HOST_OK || output.data == nullptr ||
        std::strstr(reinterpret_cast<const char *>(output.data),
            "not_implemented") == nullptr) {
        return 1;
    }
    doe_compiler_host_release(&output);
    if (output.data != nullptr || output.size != 0) {
        return 1;
    }
    std::puts("PASS: ordinary C++ host ABI consumer");
    return 0;
}
