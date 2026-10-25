/* SPDX-License-Identifier: MIT */
#ifndef DOE_NATIVE_SHADER_IMPORT_H
#define DOE_NATIVE_SHADER_IMPORT_H
#include "doe_shader.h"
#include "doe_native_shader_sources.h"

typedef struct native_shader_import {
    doe_shader_target target;
    void *data;
    size_t size;
    uint32_t workgroup_size[3];
} native_shader_import;
struct native_device;
struct native_stage;
struct native_pipeline_error;
doe_shader_status native_shader_import_create(struct native_device *,const WGPUChainedStruct *,
    native_shader_import *,doe_shader_error *);
void native_shader_import_dispose(native_shader_import *);
bool native_shader_import_select(struct native_device *,const native_shader_import *,WGPUStringView,
    uint32_t,struct native_stage *,struct native_pipeline_error *);
#endif
