CC ?= cc
AR ?= ar
include $(dir $(lastword $(MAKEFILE_LIST)))../../build-root.mk
BUILD ?= $(DOE_C_TMP)/metal
CPPFLAGS += -Iinclude
CFLAGS ?= -O2 -g
STRICT = -std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion -Wshadow
SOURCES = src/context.c src/resources.c src/commands.c src/compute_commands.c src/timestamps.c src/objc_queries.c src/objc_runtime.c \
          src/objc_pipeline.c src/objc_commands.c src/texture_formats.c src/textures.c \
          src/samplers.c src/texture_ownership.c src/texture_initialization.c src/texture_transfer.c \
          src/objc_images.c src/objc_image_transfer.c src/render_pipeline.c \
          src/render_attachments.c src/render_initialization.c src/render_draws.c src/render_commands.c src/render_visibility.c \
          src/objc_render_loader.c src/objc_render_pipeline.c src/objc_depth_state.c \
          src/objc_render_commands.c src/objc_render_draws.c
OBJECTS = $(SOURCES:src/%.c=$(BUILD)/%.o)
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
LDLIBS =
else
LDLIBS = -ldl
endif
.PHONY: all check probe sanitize clean abi-compile sdk-abi native-check native-graphics-check
all: $(BUILD)/libdoe_metal.a
$(BUILD):
	mkdir -p $@
$(BUILD)/%.o: src/%.c src/internal.h src/native.h src/objc_abi.h include/doe_metal.h include/doe_metal_texture.h src/texture_internal.h src/render_internal.h src/objc_render_types.h include/doe_metal_render.h include/doe_metal_query.h src/query_internal.h src/objc_query_types.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT) -c $< -o $@
$(BUILD)/libdoe_metal.a: $(OBJECTS)
	$(AR) rcs $@ $^
$(BUILD)/test_lifetime: tests/test_lifetime.c tests/mock_native.c tests/mock_native.h $(BUILD)/libdoe_metal.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT) tests/test_lifetime.c tests/mock_native.c $(BUILD)/libdoe_metal.a $(LDLIBS) -o $@
$(BUILD)/probe: tests/probe.c $(BUILD)/libdoe_metal.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT) $< $(BUILD)/libdoe_metal.a $(LDLIBS) -o $@
$(BUILD)/native_compute: tests/native_compute.c $(BUILD)/libdoe_metal.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT) $< $(BUILD)/libdoe_metal.a $(LDLIBS) -o $@
native-check: $(BUILD)/native_compute
	@test "$(UNAME_S)" = Darwin || { echo 'native-check requires an authorized macOS device'; exit 1; }
	DOE_MTL_ALLOW_HARDWARE=1 python3 -c 'import subprocess; subprocess.run(["$(BUILD)/native_compute", "--run"], check=True, timeout=20)'
$(BUILD)/native_graphics: tests/native_graphics.c $(BUILD)/libdoe_metal.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT) $< $(BUILD)/libdoe_metal.a $(LDLIBS) -o $@
native-graphics-check: $(BUILD)/native_graphics
	@test "$(UNAME_S)" = Darwin || { echo 'native-graphics-check requires an authorized macOS device'; exit 1; }
	DOE_MTL_ALLOW_HARDWARE=1 python3 -c 'import subprocess; subprocess.run(["$(BUILD)/native_graphics", "--run"], check=True, timeout=20)'
$(BUILD)/test_device_limits: tests/test_device_limits.c $(BUILD)/libdoe_metal.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT) $< $(BUILD)/libdoe_metal.a $(LDLIBS) -o $@
$(BUILD)/test_objc_calls: tests/test_objc_calls.c $(BUILD)/libdoe_metal.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT) $< $(BUILD)/libdoe_metal.a $(LDLIBS) -o $@
$(BUILD)/test_images: tests/test_images.c tests/mock_native.c tests/mock_native.h $(BUILD)/libdoe_metal.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT) tests/test_images.c tests/mock_native.c $(BUILD)/libdoe_metal.a $(LDLIBS) -o $@
$(BUILD)/test_objc_images: tests/test_objc_images.c $(BUILD)/libdoe_metal.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT) $< $(BUILD)/libdoe_metal.a $(LDLIBS) -o $@
$(BUILD)/test_render: tests/test_render.c tests/mock_native.c tests/mock_native.h $(BUILD)/libdoe_metal.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT) tests/test_render.c tests/mock_native.c $(BUILD)/libdoe_metal.a $(LDLIBS) -o $@
$(BUILD)/test_initialization: tests/test_initialization.c tests/mock_native.c tests/mock_native.h $(BUILD)/libdoe_metal.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT) tests/test_initialization.c tests/mock_native.c $(BUILD)/libdoe_metal.a $(LDLIBS) -o $@
$(BUILD)/test_objc_render_pipeline: tests/test_objc_render_pipeline.c $(BUILD)/libdoe_metal.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT) $< $(BUILD)/libdoe_metal.a $(LDLIBS) -o $@
$(BUILD)/test_objc_render_commands: tests/test_objc_render_commands.c $(BUILD)/libdoe_metal.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT) $< $(BUILD)/libdoe_metal.a $(LDLIBS) -o $@
$(BUILD)/test_multisample: tests/test_multisample.c tests/mock_native.c tests/mock_native.h $(BUILD)/libdoe_metal.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT) tests/test_multisample.c tests/mock_native.c $(BUILD)/libdoe_metal.a $(LDLIBS) -o $@
$(BUILD)/test_visibility: tests/test_visibility.c tests/mock_native.c tests/mock_native.h $(BUILD)/libdoe_metal.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT) tests/test_visibility.c tests/mock_native.c $(BUILD)/libdoe_metal.a $(LDLIBS) -o $@
$(BUILD)/test_timestamps: tests/test_timestamps.c tests/mock_native.c tests/mock_native.h $(BUILD)/libdoe_metal.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT) tests/test_timestamps.c tests/mock_native.c $(BUILD)/libdoe_metal.a $(LDLIBS) -o $@
$(BUILD)/test_objc_queries: tests/test_objc_queries.c $(BUILD)/libdoe_metal.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT) $< $(BUILD)/libdoe_metal.a $(LDLIBS) -o $@
check: $(BUILD)/test_initialization $(BUILD)/test_device_limits $(BUILD)/test_objc_queries $(BUILD)/test_timestamps $(BUILD)/test_visibility $(BUILD)/test_multisample $(BUILD)/test_objc_render_commands $(BUILD)/test_objc_render_pipeline $(BUILD)/test_render $(BUILD)/test_lifetime $(BUILD)/test_objc_calls $(BUILD)/test_images $(BUILD)/test_objc_images
	$(BUILD)/test_initialization
	$(BUILD)/test_device_limits
	$(BUILD)/test_objc_queries
	$(BUILD)/test_timestamps
	$(BUILD)/test_visibility
	$(BUILD)/test_multisample
	$(BUILD)/test_objc_render_commands
	$(BUILD)/test_objc_render_pipeline
	$(BUILD)/test_render
	$(BUILD)/test_lifetime
	$(BUILD)/test_objc_calls
	$(BUILD)/test_images
	$(BUILD)/test_objc_images
probe: $(BUILD)/probe
	$(BUILD)/probe
sanitize:
	$(MAKE) BUILD=$(BUILD)/sanitize CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' check
# Syntax/typed-call compilation only, NOT Apple compiler/SDK ABI verification.
abi-compile: | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT) -D__APPLE__ -D__MACH__ -D__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__=130000 -c src/objc_runtime.c -o $(BUILD)/objc_x86_64_compile.o
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT) -D__APPLE__ -D__MACH__ -D__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__=130000 -D__arm64__ -c src/objc_runtime.c -o $(BUILD)/objc_arm64_branch_compile.o
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT) -D__arm64__ -c src/objc_images.c -o $(BUILD)/objc_images_arm64_branch_compile.o
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT) -D__arm64__ -c src/objc_image_transfer.c -o $(BUILD)/objc_image_transfer_arm64_branch_compile.o
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT) -D__arm64__ -c src/objc_render_loader.c -o $(BUILD)/objc_render_loader_arm64_branch_compile.o
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT) -D__arm64__ -c src/objc_render_pipeline.c -o $(BUILD)/objc_render_pipeline_arm64_branch_compile.o
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT) -D__arm64__ -c src/objc_render_commands.c -o $(BUILD)/objc_render_commands_arm64_branch_compile.o
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT) -D__arm64__ -c src/objc_render_draws.c -o $(BUILD)/objc_render_draws_arm64_branch_compile.o
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT) -D__arm64__ -c src/objc_queries.c -o $(BUILD)/objc_queries_arm64_branch_compile.o
clean:
	rm -rf $(BUILD)
# Real SDK/type compilation for both supported macOS ABIs; no target binary runs.
sdk-abi: | $(BUILD)
	@test "$(UNAME_S)" = Darwin || { echo 'sdk-abi requires an installed Apple SDK'; exit 1; }
	xcrun --sdk macosx clang $(CPPFLAGS) $(STRICT) -fblocks -target arm64-apple-macos13 -fsyntax-only $(SOURCES) tests/sdk_abi.m
	xcrun --sdk macosx clang $(CPPFLAGS) $(STRICT) -fblocks -target x86_64-apple-macos13 -fsyntax-only $(SOURCES) tests/sdk_abi.m
