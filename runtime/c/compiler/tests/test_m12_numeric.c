/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "../src/numeric.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

_Static_assert(sizeof(NumericBuiltin) == 24u, "registry record remains bounded read-only metadata");
_Static_assert(sizeof(CompilerWorkspace) == 552u, "core layout changed");
_Static_assert(sizeof(TypeRecord) == 56u, "type layout changed");
_Static_assert(sizeof(ConstantRecord) == 32u, "constant layout changed");
_Static_assert(sizeof(ConstantValue) == 16u, "constant value layout changed");
_Static_assert(sizeof(IrInstruction) == 32u, "IR layout changed");
_Static_assert(sizeof(TargetDeclaration) == 32u, "target declaration layout changed");
_Static_assert(sizeof(LegacyWorkspace) == 146256u, "fixed workspace changed");

bool source_error(Source *source, DoeCompilerStatus status, size_t offset, const char *message) {
    source->diagnostic->status = status;
    source->diagnostic->offset = offset;
    (void)snprintf(source->diagnostic->message, sizeof(source->diagnostic->message), "%s", message);
    return false;
}

static NumericValue integer(uint64_t bits) {
    return (NumericValue){bits, IR_ABSTRACT_INT};
}

int main(void) {
    DoeCompilerDiagnostic diagnostic = {0};
    Source source = {.diagnostic = &diagnostic};
    NumericValue result = {0};
    NumericDotAccumulator sum = {0};
    const uint64_t minimum = UINT64_C(0x8000000000000000);
    const uint64_t maximum = UINT64_C(0x7fffffffffffffff);
    for (uint32_t i = 0u; i < 4u; ++i)
        assert(numeric_dot_accumulate(&source, 17u, &sum, integer(minimum), integer(minimum)));
    assert(sum.positive[4] == 1u);
    for (uint32_t i = 0u; i < 4u; ++i) assert(sum.positive[i] == 0u);
    assert(!numeric_dot_finish(&source, 17u, &sum, &result));
    assert(diagnostic.status == DOE_COMPILER_SEMANTIC_ERROR && diagnostic.offset == 17u);

    sum = (NumericDotAccumulator){0};
    assert(numeric_dot_accumulate(&source, 0u, &sum, integer(minimum), integer(maximum)));
    assert(numeric_dot_accumulate(&source, 0u, &sum, integer(minimum), integer(UINT64_C(0)-maximum)));
    assert(numeric_dot_accumulate(&source, 0u, &sum, integer(maximum), integer(maximum)));
    assert(numeric_dot_accumulate(&source, 0u, &sum, integer(maximum), integer(UINT64_C(0)-maximum)));
    assert(numeric_dot_finish(&source, 0u, &sum, &result));
    assert(result.bits == 0u && result.type == IR_ABSTRACT_INT);

    sum = (NumericDotAccumulator){0};
    assert(numeric_dot_accumulate(&source, 0u, &sum, integer(maximum), integer(1u)));
    assert(numeric_dot_accumulate(&source, 0u, &sum, integer(1u), integer(1u)));
    assert(!numeric_dot_finish(&source, 0u, &sum, &result));
    assert(numeric_dot_accumulate(&source, 0u, &sum, integer(UINT64_MAX), integer(1u)));
    assert(numeric_dot_finish(&source, 0u, &sum, &result) && result.bits == maximum);

    sum = (NumericDotAccumulator){0};
    assert(numeric_dot_accumulate(&source, 0u, &sum, integer(minimum), integer(1u)));
    assert(numeric_dot_finish(&source, 0u, &sum, &result) && result.bits == minimum);
    assert(numeric_dot_accumulate(&source, 0u, &sum, integer(UINT64_MAX), integer(1u)));
    assert(!numeric_dot_finish(&source, 0u, &sum, &result));
    puts("M12 exact accumulator controls passed");
    return 0;
}
