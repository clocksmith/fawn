/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "../include/doe_compiler_module.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>

int main() {
    DoeCompilerModuleCapacities capacities{};
    capacities.typed.source_bytes=1024;
    capacities.typed.tokens=128;
    capacities.typed.statements=8;
    capacities.typed.expressions=32;
    capacities.typed.types=8;
    capacities.typed.members=8;
    capacities.typed.resources=4;
    capacities.typed.access_steps=32;
    capacities.typed.constants=16;
    capacities.typed.constant_edges=16;
    capacities.typed.ir_instructions=128;
    capacities.typed.ir_operands=32;
    capacities.typed.target_declarations=64;
    capacities.typed.traversal_items=16;
    capacities.typed.words=2048;
    capacities.typed.expression_depth=16;
    capacities.typed.control_depth=8;
    capacities.typed.type_syntax_depth=16;
    capacities.entries=2;
    capacities.entry_inputs=10;
    capacities.entry_references=8;
    DoeCompilerWorkspaceRequirements requirements{};
    assert(doe_compiler_module_requirements(5,&capacities,sizeof(capacities),&requirements,sizeof(requirements))==DOE_COMPILER_OK);
    void *workspace=std::malloc(requirements.size_bytes);
    assert(workspace!=nullptr);
    const char source[]="@compute @workgroup_size(1) fn first(){}@compute @workgroup_size(7) fn second(){}";
    DoeCompilerEntrySelectionV5 selection{};
    selection.mode=DOE_COMPILER_SELECT_NAME;
    selection.name="second";
    selection.name_length=6;
    DoeCompilerModuleResult result{};
    assert(doe_compiler_compile_module(5,&capacities,sizeof(capacities),workspace,requirements.size_bytes,
        source,std::strlen(source),&selection,sizeof(selection),7,nullptr,0,&result,sizeof(result))==DOE_COMPILER_OK);
    assert(result.required_is_exact==1 && result.required.entries==2 && result.published.entries==0);
    std::free(workspace);
    std::cout<<"ABI5 C++17 native consumer passed\n";
}
