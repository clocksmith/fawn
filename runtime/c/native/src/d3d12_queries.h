/* SPDX-License-Identifier: MIT */
#ifndef DOE_NATIVE_D3D12_QUERIES_H
#define DOE_NATIVE_D3D12_QUERIES_H
#include "d3d12_frontend.h"
#include "../../backends/d3d12/include/doe_d3d12_graphics.h"
native_failure native_d3d12_timestamp_initialize(native_device *);
void native_d3d12_timestamp_close(native_device *);
native_failure native_d3d12_queries_create(native_device *,WGPUQueryType,uint32_t,doe_dx_queries **);
void native_d3d12_queries_release(doe_dx_queries *);
native_failure native_d3d12_record_query(native_command_handle *,const doe_resource_command *);
#endif
