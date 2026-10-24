CC ?= cc
AR ?= ar
include $(dir $(lastword $(MAKEFILE_LIST)))../../build-root.mk
BUILD ?= $(DOE_C_TMP)/d3d12
CPPFLAGS += -Iinclude
CFLAGS ?= -O2 -g
STRICT = -std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion -Wshadow
OWNER = src/command_storage.c src/context.c src/resources.c src/commands.c src/compute.c src/pipeline.c src/textures.c src/texture_transfer.c src/texture_regions.c src/samplers.c src/render_pipeline.c src/render_commands.c src/bindings.c src/graphics.c src/timestamps.c src/vertex_layout.c src/geometry.c
WINDOWS = src/windows_context.c src/windows_caps.c src/windows_adapter.c src/windows_resources.c src/windows_commands.c src/windows_pipeline.c src/windows_bindings.c src/windows_textures.c src/windows_texture_regions.c src/windows_samplers.c src/windows_transitions.c src/windows_render_pipeline.c src/windows_render_commands.c src/windows_graphics.c src/windows_timestamps.c src/windows_geometry.c
HOST_OBJECTS = $(OWNER:src/%.c=$(BUILD)/host/%.o)
WINDOWS_OBJECTS = $(OWNER:src/%.c=$(BUILD)/windows/%.o) $(WINDOWS:src/%.c=$(BUILD)/windows/%.o)
HEADERS = include/doe_d3d12_render.h include/doe_d3d12_dynamic.h include/doe_d3d12_texture_regions.h include/doe_d3d12_mesh.h include/doe_d3d12_graphics.h include/doe_d3d12_texture.h ../../common/include/doe_copy_footprint.h include/doe_d3d12.h src/internal.h src/native.h src/windows_internal.h
.PHONY: all check sanitize windows native-fixture
# On other platforms only the explicitly host-scoped owner tests are available.
ifeq ($(OS),Windows_NT)
all: windows
else
all: check
endif
$(BUILD)/host $(BUILD)/windows:
	mkdir -p $@
$(BUILD)/host/%.o: src/%.c $(HEADERS) | $(BUILD)/host
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT) -Dcalloc=doe_dx_test_calloc -Dfree=doe_dx_test_free -c $< -o $@
$(BUILD)/test_owner: tests/test_owner.c tests/mock_native.c tests/mock_native.h $(HOST_OBJECTS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT) tests/test_owner.c tests/mock_native.c $(HOST_OBJECTS) -o $@
$(BUILD)/test_textures: tests/test_textures.c tests/mock_native.c tests/mock_native.h $(HOST_OBJECTS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT) tests/test_textures.c tests/mock_native.c $(HOST_OBJECTS) -o $@
$(BUILD)/test_graphics: tests/test_graphics.c tests/mock_native.c tests/mock_native.h $(HOST_OBJECTS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT) tests/test_graphics.c tests/mock_native.c $(HOST_OBJECTS) -o $@
$(BUILD)/test_mesh: tests/test_mesh.c tests/mock_native.c tests/mock_native.h $(HOST_OBJECTS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT) tests/test_mesh.c tests/mock_native.c $(HOST_OBJECTS) -o $@
$(BUILD)/test_indirect: tests/test_indirect.c tests/mock_native.c tests/mock_native.h $(HOST_OBJECTS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT) tests/test_indirect.c tests/mock_native.c $(HOST_OBJECTS) -o $@
$(BUILD)/test_uniform: tests/test_uniform.c tests/mock_native.c tests/mock_native.h $(HOST_OBJECTS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT) tests/test_uniform.c tests/mock_native.c $(HOST_OBJECTS) -o $@
$(BUILD)/test_adapter_selection: tests/test_adapter_selection.c tests/mock_native.c tests/mock_native.h $(HOST_OBJECTS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT) tests/test_adapter_selection.c tests/mock_native.c $(HOST_OBJECTS) -o $@
$(BUILD)/test_limits: tests/test_limits.c tests/mock_native.c tests/mock_native.h $(HOST_OBJECTS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT) tests/test_limits.c tests/mock_native.c $(HOST_OBJECTS) -o $@
$(BUILD)/test_aliases: tests/test_aliases.c tests/mock_native.c tests/mock_native.h $(HOST_OBJECTS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT) tests/test_aliases.c tests/mock_native.c $(HOST_OBJECTS) -o $@
$(BUILD)/test_dynamic: tests/test_dynamic.c tests/mock_native.c tests/mock_native.h $(HOST_OBJECTS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT) tests/test_dynamic.c tests/mock_native.c $(HOST_OBJECTS) -o $@
check: $(BUILD)/test_render $(BUILD)/test_sampled $(BUILD)/test_texture_regions $(BUILD)/test_dynamic $(BUILD)/test_aliases $(BUILD)/test_limits $(BUILD)/test_adapter_selection $(BUILD)/test_owner $(BUILD)/test_textures $(BUILD)/test_graphics $(BUILD)/test_mesh $(BUILD)/test_indirect $(BUILD)/test_uniform
	$(BUILD)/test_render
	$(BUILD)/test_sampled
	$(BUILD)/test_texture_regions
	$(BUILD)/test_dynamic
	$(BUILD)/test_aliases
	$(BUILD)/test_limits
	$(BUILD)/test_adapter_selection
	$(BUILD)/test_owner
	$(BUILD)/test_textures
	$(BUILD)/test_graphics
	$(BUILD)/test_mesh
	$(BUILD)/test_indirect
	$(BUILD)/test_uniform
sanitize:
	$(MAKE) BUILD=$(BUILD)/sanitize CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' check
$(BUILD)/windows/%.o: src/%.c $(HEADERS) | $(BUILD)/windows
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT) -DDOE_DX_REQUIRE_WINDOWS -c $< -o $@
$(BUILD)/libdoe_d3d12.a: $(WINDOWS_OBJECTS)
	$(AR) rcs $@ $^
windows: $(BUILD)/libdoe_d3d12.a
# Build only. Executing this requires separately authorized Windows/GPU work.
native-fixture: $(BUILD)/native_compute.exe $(BUILD)/native_texture.exe $(BUILD)/native_graphics.exe $(BUILD)/native_mesh.exe
$(BUILD)/native_%.exe: tests/native_%.c tests/native_fixture.h $(BUILD)/libdoe_d3d12.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT) $< $(BUILD)/libdoe_d3d12.a -ld3d12 -ldxgi -ldxguid -o $@

# Build only against actual SDK declarations; host controls never spoof Windows.
$(BUILD)/test_windows_caps.exe: tests/test_windows_caps.c $(HEADERS) $(BUILD)/libdoe_d3d12.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT) $< $(BUILD)/libdoe_d3d12.a -ld3d12 -ldxgi -ldxguid -o $@
.PHONY: sdk-control
sdk-control: $(BUILD)/test_windows_caps.exe

$(BUILD)/test_texture_regions: tests/test_texture_regions.c tests/mock_native.c tests/mock_native.h $(HOST_OBJECTS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT) tests/test_texture_regions.c tests/mock_native.c $(HOST_OBJECTS) -o $@

$(BUILD)/test_windows_texture_regions.exe: tests/test_windows_texture_regions.c src/windows_texture_regions.c $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT) tests/test_windows_texture_regions.c src/windows_texture_regions.c -o $@

$(BUILD)/test_sampled: tests/test_sampled.c tests/mock_native.c tests/mock_native.h $(HOST_OBJECTS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT) tests/test_sampled.c tests/mock_native.c $(HOST_OBJECTS) -o $@

$(BUILD)/test_windows_sampled.exe: tests/test_windows_sampled.c $(HEADERS) $(BUILD)/libdoe_d3d12.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT) $< $(BUILD)/libdoe_d3d12.a -ld3d12 -ldxgi -ldxguid -o $@

$(BUILD)/test_render: tests/test_render.c tests/mock_native.c tests/mock_native.h $(HOST_OBJECTS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT) tests/test_render.c tests/mock_native.c $(HOST_OBJECTS) -o $@

$(BUILD)/test_windows_render.exe: tests/test_windows_render.c $(HEADERS) $(BUILD)/libdoe_d3d12.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT) $< $(BUILD)/libdoe_d3d12.a -ld3d12 -ldxgi -ldxguid -o $@
