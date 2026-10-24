/* SPDX-License-Identifier: MIT
 * Typed native-call ledger only; no shader interpretation or GPU result. */
#include "mock_native.h"
#include "../src/internal.h"
#include "../include/doe_metal_indirect.h"
#include <assert.h>
#include <stdio.h>

static doe_mtl_pipeline_desc description(const char *name) {
    return (doe_mtl_pipeline_desc){.msl="borrowed validated compiler artifact",.msl_length=36,
        .entrypoint=name,.workgroup={1,1,1},.sizes_slot=30};
}
static doe_mtl_dispatch_guard_desc guard_description(void) {
    return (doe_mtl_dispatch_guard_desc){.pipeline=description("guard"),.binding_slot=3,
        .max_groups={65535,65535,65535}};
}
static void run(bool terminal_error) {
    mock_native mock;
    doe_mtl_context *context = NULL;
    assert(doe_mtl_context_open(mock_driver(&mock),&context,NULL) == DOE_MTL_OK);
    doe_mtl_dispatch_guard *guard = NULL;
    doe_mtl_dispatch_guard_desc desc = guard_description();
    assert(doe_mtl_dispatch_guard_create(context,&desc,&guard,NULL) == DOE_MTL_OK);
    doe_mtl_pipeline *pipeline = NULL;
    doe_mtl_pipeline_desc target = description("consumer");
    target.workgroup = (doe_mtl_dimensions){4,2,1};
    assert(doe_mtl_pipeline_create(context,&target,&pipeline,NULL) == DOE_MTL_OK);
    doe_mtl_buffer *arguments = NULL, *output = NULL;
    assert(doe_mtl_buffer_create(context,64,DOE_MTL_SHARED,&arguments,NULL) == DOE_MTL_OK);
    assert(doe_mtl_buffer_create(context,64,DOE_MTL_SHARED,&output,NULL) == DOE_MTL_OK);
    void *argument_identity = doe_mtl_buffer_native(arguments);
    void *output_identity = doe_mtl_buffer_native(output);
    void *pipeline_identity = doe_mtl_pipeline_native(pipeline);
    doe_mtl_buffer_binding binding = {7,output,8,16};
    doe_mtl_indirect_dispatch dispatch = {.guard=guard,.pipeline=pipeline,.buffers=&binding,
        .buffer_count=1,.arguments=arguments,.argument_offset=4};
    doe_mtl_commands *commands = NULL;
    assert(doe_mtl_commands_create(context,&commands,NULL) == DOE_MTL_OK);
    unsigned queries = mock.content_queries;
    assert(doe_mtl_commands_dispatch_indirect(commands,&dispatch,NULL) == DOE_MTL_OK);
    assert(mock.content_queries == queries); /* No host argument/scratch read. */
    assert(mock.copies == 1 && mock.last_source == argument_identity && mock.last_source_offset == 4);
    assert(mock.last_length == 12 && mock.last_destination_offset == 0);
    assert(mock.last_destination != argument_identity && mock.last_destination != output_identity);
    assert(mock.dispatches == 1 && mock.indirects == 1);
    assert(mock.copy_sequence < mock.dispatch_sequence && mock.dispatch_sequence < mock.indirect_sequence);
    assert(mock.direct_resources.buffers[3].buffer == mock.last_destination);
    assert(mock.direct_resources.buffers[3].offset == 0 && mock.direct_resources.buffers[3].size == 12);
    assert(mock.indirect_arguments == mock.last_destination && mock.indirect_offset == 0);
    assert(mock.indirect_pipeline == pipeline_identity && mock.bindings[7].buffer == output_identity);
    assert(mock.bindings[7].offset == 8 && mock.bindings[7].size == 16);
    assert(mock.workgroup.x == 4 && mock.workgroup.y == 2 && mock.workgroup.z == 1);
    doe_mtl_dispatch_guard_release(guard);
    doe_mtl_pipeline_release(pipeline);
    doe_mtl_buffer_release(arguments);
    doe_mtl_buffer_release(output);
    doe_mtl_context_release(context);
    mock.unknown_commit = true;
    assert(doe_mtl_commands_submit(commands,NULL) == DOE_MTL_OK);
    unsigned live = mock.live;
    assert(doe_mtl_commands_poll(commands,NULL) == DOE_MTL_COMPLETION_UNKNOWN);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_BUSY && mock.live == live);
    mock_result(&mock,terminal_error ? 5 : 4,-83);
    doe_mtl_error error;
    assert(doe_mtl_commands_poll(commands,&error) == (terminal_error ? DOE_MTL_EXECUTION_FAILED : DOE_MTL_OK));
    if (terminal_error) assert(error.native_code == -83);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
    mock_assert_closed(&mock);
}
static void failures(void) {
    const mock_operation faults[] = {MOCK_BUFFER,MOCK_COPY,MOCK_DISPATCH,MOCK_INDIRECT};
    for (size_t i = 0; i < sizeof(faults)/sizeof(*faults); ++i) {
        mock_native mock;
        doe_mtl_context *context = NULL;
        assert(doe_mtl_context_open(mock_driver(&mock),&context,NULL) == DOE_MTL_OK);
        doe_mtl_dispatch_guard *guard = NULL;
        doe_mtl_dispatch_guard_desc desc = guard_description();
        doe_mtl_dispatch_guard *intact = (doe_mtl_dispatch_guard *)&desc;
        desc.max_groups.y = 0;
        assert(doe_mtl_dispatch_guard_create(context,&desc,&intact,NULL) == DOE_MTL_INVALID_ARGUMENT);
        assert(intact == (doe_mtl_dispatch_guard *)&desc);
        desc = guard_description();
        assert(doe_mtl_dispatch_guard_create(context,&desc,&guard,NULL) == DOE_MTL_OK);
        doe_mtl_pipeline *pipeline = NULL;
        doe_mtl_pipeline_desc target = description("consumer");
        assert(doe_mtl_pipeline_create(context,&target,&pipeline,NULL) == DOE_MTL_OK);
        doe_mtl_buffer *arguments = NULL;
        assert(doe_mtl_buffer_create(context,64,DOE_MTL_SHARED,&arguments,NULL) == DOE_MTL_OK);
        doe_mtl_indirect_dispatch dispatch = {.guard=guard,.pipeline=pipeline,.arguments=arguments,.argument_offset=4};
        doe_mtl_commands *commands = NULL;
        assert(doe_mtl_commands_create(context,&commands,NULL) == DOE_MTL_OK);
        mock.fail = faults[i];
        doe_mtl_error error = {0};
        assert(doe_mtl_commands_dispatch_indirect(commands,&dispatch,&error) == DOE_MTL_NATIVE_FAILURE);
        assert(error.native_code == -71 && !strcmp(error.message,"owned fault boundary"));
        assert(doe_mtl_commands_submit(commands,&error) == DOE_MTL_NATIVE_FAILURE && mock.commits == 0);
        assert(error.native_code == -71 && !strcmp(error.message,"owned fault boundary"));
        doe_mtl_dispatch_guard_release(guard);
        doe_mtl_pipeline_release(pipeline);
        doe_mtl_buffer_release(arguments);
        doe_mtl_context_release(context);
        assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
        mock_assert_closed(&mock);
    }
}
static void admission(void) {
    mock_native mock, foreign;
    doe_mtl_context *context = NULL, *other = NULL;
    assert(doe_mtl_context_open(mock_driver(&mock),&context,NULL) == DOE_MTL_OK);
    assert(doe_mtl_context_open(mock_driver(&foreign),&other,NULL) == DOE_MTL_OK);
    doe_mtl_dispatch_guard *guard = NULL;
    doe_mtl_dispatch_guard_desc desc = guard_description();
    assert(doe_mtl_dispatch_guard_create(context,&desc,&guard,NULL) == DOE_MTL_OK);
    doe_mtl_pipeline *pipeline = NULL;
    doe_mtl_pipeline_desc target = description("consumer");
    assert(doe_mtl_pipeline_create(context,&target,&pipeline,NULL) == DOE_MTL_OK);
    doe_mtl_buffer *arguments = NULL, *uninitialized = NULL, *foreign_arguments = NULL;
    assert(doe_mtl_buffer_create(context,16,DOE_MTL_SHARED,&arguments,NULL) == DOE_MTL_OK);
    assert(doe_mtl_buffer_create(context,16,DOE_MTL_PRIVATE,&uninitialized,NULL) == DOE_MTL_OK);
    assert(doe_mtl_buffer_create(other,16,DOE_MTL_SHARED,&foreign_arguments,NULL) == DOE_MTL_OK);
    const doe_mtl_status expected[] = {DOE_MTL_ALIGNMENT,DOE_MTL_RANGE,DOE_MTL_UNINITIALIZED,
        DOE_MTL_DEVICE_MISMATCH,DOE_MTL_UNSUPPORTED};
    for (size_t i = 0; i < sizeof(expected)/sizeof(*expected); ++i) {
        doe_mtl_commands *commands = NULL;
        assert(doe_mtl_commands_create(context,&commands,NULL) == DOE_MTL_OK);
        doe_mtl_indirect_dispatch dispatch = {.guard=guard,.pipeline=pipeline,.arguments=arguments};
        if (i == 0) dispatch.argument_offset = 2;
        if (i == 1) dispatch.argument_offset = 8;
        if (i == 2) dispatch.arguments = uninitialized;
        if (i == 3) dispatch.arguments = foreign_arguments;
        if (i == 4) context->native.dispatch_indirect = NULL;
        assert(doe_mtl_commands_dispatch_indirect(commands,&dispatch,NULL) == expected[i]);
        assert(!mock.copies && !mock.dispatches && !mock.indirects && !arguments->owner);
        assert(doe_mtl_commands_submit(commands,NULL) == expected[i] && !mock.commits);
        assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
    }
    doe_mtl_buffer_release(foreign_arguments);
    doe_mtl_buffer_release(uninitialized);
    doe_mtl_buffer_release(arguments);
    doe_mtl_dispatch_guard_release(guard);
    doe_mtl_pipeline_release(pipeline);
    doe_mtl_context_release(other);
    doe_mtl_context_release(context);
    mock_assert_closed(&mock);
    mock_assert_closed(&foreign);
}
int main(void) {
    run(false);
    run(true);
    failures();
    admission();
    puts("PASS guarded Metal indirect call ledger: GPU copy/guard/indirect order, exact resources, no CPU reads, early release, unknown retention and first native failure; no GPU execution");
}
