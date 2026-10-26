/* SPDX-License-Identifier: MIT */
#include "../include/doe_compiler_diagnostics.h"
#include <cassert>
#include <cstring>
#include <type_traits>

static_assert(std::is_standard_layout<DoeCompilerNoticeV18>::value, "notice C layout");
static_assert(std::is_trivially_copyable<DoeCompilerNoticeV18>::value, "notice owns only values");
static_assert(sizeof(DoeCompilerNoticeV18)==216, "versioned notice record");
int main()
{
    DoeCompilerDiagnosticResultV18 result;
    std::memset(&result,0xa5,sizeof(result));
    const auto before=result;
    assert(doe_compiler_validate_module_diagnostics(17,nullptr,sizeof(DoeCompilerSpecializationCapacities),
        nullptr,0,nullptr,0,0,nullptr,0,&result,sizeof(result))==DOE_COMPILER_ABI_MISMATCH);
    assert(std::memcmp(&result,&before,sizeof(result))==0);
    assert(doe_compiler_validate_module_diagnostics(18,nullptr,sizeof(DoeCompilerSpecializationCapacities),
        nullptr,0,nullptr,0,0,nullptr,0,&result,sizeof(result))==DOE_COMPILER_INPUT_ERROR);
    assert(result.required_notices==0 && result.published_notices==0);
    return 0;
}
