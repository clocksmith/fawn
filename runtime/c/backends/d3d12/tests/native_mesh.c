/* SPDX-License-Identifier: MIT */
#include "native_fixture.h"
#include "doe_d3d12_mesh.h"

/* Actual Windows fixture: two indexed full-screen triangles at different depths.
 * Per-instance color is blue for the near triangle and red for the far triangle.
 * LESS depth testing preserves blue and 0.25 depth, verified by both readbacks. */
int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: native_mesh.exe vertex.dxil fragment.dxil\n");
        return 2;
    }
    size_t vertex_size = 0, fragment_size = 0;
    unsigned char *vertex = read_bytecode(argv[1], &vertex_size);
    unsigned char *fragment = read_bytecode(argv[2], &fragment_size);
    if (!vertex || !fragment) {
        free(vertex);
        free(fragment);
        return 2;
    }
    doe_dx_context *context = NULL;
    doe_dx_commands *commands = NULL;
    doe_dx_pipeline *pipeline = NULL;
    doe_dx_texture *color = NULL, *depth = NULL;
    doe_dx_buffer *upload = NULL, *vertices = NULL, *instances = NULL, *indices = NULL;
    doe_dx_buffer *pixels = NULL, *depths = NULL;
    doe_dx_error error = {0};
    doe_dx_status status = doe_dx_context_create(NULL, &context, &error);
    int result = 1;
    if (status != DOE_DX_OK) goto failed;
    doe_dx_caps caps;
    status = doe_dx_context_caps(context, &caps);
    if (status != DOE_DX_OK) goto failed;
    printf("adapter=%s vendor=%u device=%u LUID=%ld:%lu software=%d\n", caps.name,
        caps.vendor_id, caps.device_id, (long)caps.luid_high, (unsigned long)caps.luid_low,
        caps.software_adapter ? 1 : 0);
    doe_dx_texture_desc texture = {64, 2, DOE_DX_BGRA8_UNORM,
        DOE_DX_TEXTURE_RENDER_ATTACHMENT | DOE_DX_TEXTURE_COPY_SRC};
    status = doe_dx_texture_create(context, &texture, &color, &error);
    if (status != DOE_DX_OK) goto failed;
    texture.format = DOE_DX_D32_FLOAT;
    texture.usage = DOE_DX_TEXTURE_DEPTH_ATTACHMENT | DOE_DX_TEXTURE_COPY_SRC;
    status = doe_dx_texture_create(context, &texture, &depth, &error);
    if (status != DOE_DX_OK) goto failed;
    const float positions[18] = {
        -1, -1, 0.25f, -1, 3, 0.25f, 3, -1, 0.25f,
        -1, -1, 0.75f, -1, 3, 0.75f, 3, -1, 0.75f
    };
    const float colors[8] = {0, 0, 1, 1, 1, 0, 0, 1};
    const uint16_t index_data[6] = {0, 1, 2, 3, 4, 5};
    unsigned char staging[116];
    memcpy(staging, positions, sizeof(positions));
    memcpy(staging + 72, colors, sizeof(colors));
    memcpy(staging + 104, index_data, sizeof(index_data));
    status = doe_dx_buffer_create(context, sizeof(staging), DOE_DX_UPLOAD, &upload, &error);
    if (status != DOE_DX_OK) goto failed;
    status = doe_dx_buffer_write(upload, 0, staging, sizeof(staging), &error);
    if (status != DOE_DX_OK) goto failed;
    status = doe_dx_buffer_create(context, sizeof(positions), DOE_DX_DEFAULT, &vertices, &error);
    if (status != DOE_DX_OK) goto failed;
    status = doe_dx_buffer_create(context, sizeof(colors), DOE_DX_DEFAULT, &instances, &error);
    if (status != DOE_DX_OK) goto failed;
    status = doe_dx_buffer_create(context, sizeof(index_data), DOE_DX_DEFAULT, &indices, &error);
    if (status != DOE_DX_OK) goto failed;
    status = doe_dx_buffer_create(context, 512, DOE_DX_READBACK, &pixels, &error);
    if (status != DOE_DX_OK) goto failed;
    status = doe_dx_buffer_create(context, 512, DOE_DX_READBACK, &depths, &error);
    if (status != DOE_DX_OK) goto failed;
    doe_dx_vertex_layout layouts[2] = {{12, DOE_DX_PER_VERTEX}, {16, DOE_DX_PER_INSTANCE}};
    doe_dx_vertex_attribute attributes[2] = {{0, 0, 0, DOE_DX_FLOAT32X3}, {1, 1, 0, DOE_DX_FLOAT32X4}};
    doe_dx_mesh_pipeline_desc pipeline_desc = {
        {vertex, vertex_size, {{fragment, fragment_size, NULL, 0}, NULL, 0, NULL, 0}, DOE_DX_BGRA8_UNORM},
        layouts, 2, attributes, 2, true, true, DOE_DX_LESS
    };
    status = doe_dx_pipeline_create_mesh(context, &pipeline_desc, &pipeline, &error);
    if (status != DOE_DX_OK) goto failed;
    free(vertex);
    vertex = NULL;
    free(fragment);
    fragment = NULL;
    status = doe_dx_commands_create(context, &commands, &error);
    if (status != DOE_DX_OK) goto failed;
    status = doe_dx_commands_copy(commands, upload, 0, vertices, 0, sizeof(positions), &error);
    if (status != DOE_DX_OK) goto failed;
    status = doe_dx_commands_copy(commands, upload, 72, instances, 0, sizeof(colors), &error);
    if (status != DOE_DX_OK) goto failed;
    status = doe_dx_commands_copy(commands, upload, 104, indices, 0, sizeof(index_data), &error);
    if (status != DOE_DX_OK) goto failed;
    doe_dx_buffer_binding bindings[2] = {{vertices, 0, 72}, {instances, 0, 32}};
    doe_dx_index_binding index = {{indices, 0, 12}, DOE_DX_INDEX_UINT16, 0, 3};
    doe_dx_mesh_draw_desc draw = {{color, true, {1, 1, 1, 1}, 0, 1, 0, 0},
        bindings, 2, &index, depth, true, 1};
    doe_dx_compute_resources resources = {0};
    status = doe_dx_commands_draw_mesh(commands, pipeline, &resources, &draw, &error);
    if (status != DOE_DX_OK) goto failed;
    draw.draw.clear = false;
    draw.clear_depth = false;
    draw.draw.first_instance = 1;
    index.first = 3;
    status = doe_dx_commands_draw_mesh(commands, pipeline, &resources, &draw, &error);
    if (status != DOE_DX_OK) goto failed;
    status = doe_dx_commands_read_texture(commands, color, pixels, (doe_dx_texture_layout){0}, &error);
    if (status != DOE_DX_OK) goto failed;
    status = doe_dx_commands_read_texture(commands, depth, depths, (doe_dx_texture_layout){0}, &error);
    if (status != DOE_DX_OK) goto failed;
    doe_dx_buffer_release(upload);
    upload = NULL;
    doe_dx_buffer_release(vertices);
    vertices = NULL;
    doe_dx_buffer_release(instances);
    instances = NULL;
    doe_dx_buffer_release(indices);
    indices = NULL;
    doe_dx_texture_release(color);
    color = NULL;
    doe_dx_texture_release(depth);
    depth = NULL;
    doe_dx_pipeline_release(pipeline);
    pipeline = NULL;
    status = doe_dx_commands_submit(commands, &error);
    if (status != DOE_DX_OK && status != DOE_DX_COMPLETION_UNKNOWN) goto failed;
    doe_dx_commands_release(commands);
    commands = NULL;
    status = doe_dx_context_poll(context, 30000, &error);
    if (status != DOE_DX_OK) goto failed;
    unsigned char actual[512];
    float depth_values[128];
    status = doe_dx_buffer_read(pixels, 0, actual, sizeof(actual), &error);
    if (status != DOE_DX_OK) goto failed;
    status = doe_dx_buffer_read(depths, 0, depth_values, sizeof(depth_values), &error);
    if (status != DOE_DX_OK) goto failed;
    for (size_t pixel = 0; pixel < 128; pixel++) {
        if (actual[pixel * 4] != 255 || actual[pixel * 4 + 1] || actual[pixel * 4 + 2] ||
            actual[pixel * 4 + 3] != 255 || depth_values[pixel] != 0.25f) {
            fprintf(stderr, "indexed mesh/depth mismatch at pixel %zu\n", pixel);
            goto cleanup;
        }
    }
    puts("native indexed vertex/instance draw passed: exact BGRA blue and D32 0.25 readback");
    result = 0;
    goto cleanup;
failed:
    report("native mesh/depth", status, &error);
cleanup:
    free(vertex);
    free(fragment);
    doe_dx_commands_release(commands);
    doe_dx_pipeline_release(pipeline);
    doe_dx_texture_release(color);
    doe_dx_texture_release(depth);
    doe_dx_buffer_release(upload);
    doe_dx_buffer_release(vertices);
    doe_dx_buffer_release(instances);
    doe_dx_buffer_release(indices);
    doe_dx_buffer_release(pixels);
    doe_dx_buffer_release(depths);
    if (context) {
        doe_dx_status closed = doe_dx_context_destroy(context);
        if (closed != DOE_DX_OK) {
            fprintf(stderr, "unresolved native work retained; context destruction refused (%d)\n", (int)closed);
            result = 1;
        }
    }
    return result;
}
