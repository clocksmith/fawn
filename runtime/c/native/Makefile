CC = gcc
CXX = g++
include $(dir $(lastword $(MAKEFILE_LIST)))../build-root.mk
BUILD ?= $(DOE_C_TMP)/native
include platform.mk
NATIVE_LIBRARY ?= $(BUILD)/libwebgpu_doe_c.$(SHARED_SUFFIX)
COMPILER_LIBRARY ?= $(BUILD)/libdoe_compiler.$(SHARED_SUFFIX)
SANITIZE ?=
# One owner list drives source, public include and private header dependencies.
include sources.mk
OWNER_DIRS = $(NATIVE_OWNER_DIRS)
# The compute-program contract is the package schema's value, never a copied number.
PROGRAM_SCHEMA ?= ../../../config/compute-program.schema.json
PROGRAM_CONTRACT := $(shell python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["$$defs"]["nativeContractVersion"]["const"])' $(PROGRAM_SCHEMA) 2>/dev/null)
ifeq ($(PROGRAM_CONTRACT),)
$(error cannot read nativeContractVersion from $(PROGRAM_SCHEMA))
endif
CPPFLAGS += -D_POSIX_C_SOURCE=200809L -DWGPU_SHARED_LIBRARY -DWGPU_IMPLEMENTATION \
 -DDOE_COMPUTE_PROGRAM_CONTRACT=$(PROGRAM_CONTRACT)u -Iinclude \
 -I../../zig/vendor/webgpu-headers -I../compiler/include $(addprefix -I,$(addsuffix /include,$(OWNER_DIRS)))
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Werror -fPIC -pthread $(SANITIZE)
SOURCES = $(wildcard src/*.c) $(foreach owner,$(OWNER_DIRS),$(wildcard $(owner)/src/*.c))
OBJECTS = $(patsubst src/%.c,$(BUILD)/native/%.o,$(filter src/%,$(SOURCES))) \
 $(patsubst ../%.c,$(BUILD)/owners/%.o,$(filter ../%,$(SOURCES)))
HEADERS = sources.mk $(wildcard src/*.h include/*.h ../compiler/include/*.h ../common/include/*.h ../backends/d3d12/include/*.h) $(PROGRAM_SCHEMA) \
 $(foreach owner,$(OWNER_DIRS),$(wildcard $(owner)/include/*.h $(owner)/src/*.h)) \
 ../../zig/vendor/webgpu-headers/webgpu.h
COMPILER_RUNPATH ?= $(abspath $(dir $(COMPILER_LIBRARY)))
COMPILER_LINK = -L$(abspath $(dir $(COMPILER_LIBRARY))) $(COMPILER_NAME_LINK) '-Wl,-rpath,$(COMPILER_RUNPATH)'
.PHONY: all check check-build-dependencies check-empty-module clean
all: $(NATIVE_LIBRARY)
.PHONY: native-library-name
native-library-name:
	@printf '%s\n' '$(notdir $(NATIVE_LIBRARY))'
$(BUILD):
	mkdir -p $@
.PHONY: compiler-shared
compiler-shared:
$(BUILD)/libdoe_compiler.$(SHARED_SUFFIX): compiler-shared | $(BUILD)
	$(MAKE) -C ../compiler shared SHARED_LIBRARY="$(abspath $@)" \
	  SHARED_PLATFORM="$(SHARED_PLATFORM)" TMPDIR="$(abspath $(BUILD))/tmp" EXTRA_CFLAGS="$(SANITIZE)"
$(BUILD)/native/%.o: src/%.c $(HEADERS) Makefile platform.mk
	mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -fvisibility=hidden -c $< -o $@
$(BUILD)/owners/%.o: ../%.c $(HEADERS) Makefile platform.mk
	mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -fvisibility=hidden -c $< -o $@
$(NATIVE_LIBRARY): $(OBJECTS) $(COMPILER_LIBRARY) $(NATIVE_EXPORTS) | $(BUILD)
	$(CC) $(CFLAGS) $(NATIVE_SHARED_FLAGS) $(OBJECTS) $(COMPILER_LINK) $(PLATFORM_LIBS) -o $@
$(BUILD)/native.exports: EXPORTED_SYMBOLS.txt EXTENSION_SYMBOLS.txt | $(BUILD)
	cat $^ | sed 's/^/_/' > $@.tmp
	mv $@.tmp $@
$(BUILD)/backend-selection: tests/backend_selection.c tests/consumer.h tests/adapter_class.h $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
$(BUILD)/metal-bridge: tests/metal_bridge.c tests/metal_vertex_cases.h tests/metal_graphics_cases.h tests/mixed_resource_cases.h tests/metal_mixed_host.h tests/consumer.h tests/adapter_class.h $(OBJECTS) $(COMPILER_LIBRARY) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/metal_bridge.c ../backends/metal/tests/mock_native.c $(OBJECTS) $(COMPILER_LINK) $(PLATFORM_LIBS) \
	  -Wl,--wrap=doe_mtl_context_create -Wl,--wrap=doe_mtl_context_device -Wl,--wrap=doe_mtl_commands_create \
	  -Wl,--wrap=doe_mtl_commands_wait -Wl,--wrap=doe_mtl_buffer_create \
	  -Wl,--wrap=doe_vk_texture_format_caps -o $@
$(BUILD)/draw-budget: tests/draw_budget.c tests/consumer.h tests/adapter_class.h $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
$(BUILD)/indirect-render: tests/indirect_render.c tests/consumer.h tests/adapter_class.h $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
$(BUILD)/indexed-render: tests/indexed_render.c tests/consumer.h tests/adapter_class.h $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
$(BUILD)/depth-copies: tests/depth_copies.c tests/consumer.h tests/adapter_class.h $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
$(BUILD)/depth-sampling: tests/depth_sampling.c tests/consumer.h tests/adapter_class.h $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
$(BUILD)/cube-array: tests/cube_array.c tests/consumer.h tests/adapter_class.h $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -lm -o $@
$(BUILD)/view-formats: tests/view_formats.c tests/consumer.h tests/adapter_class.h $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -lm -o $@
$(BUILD)/point-list: tests/point_list.c tests/consumer.h tests/adapter_class.h $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
$(BUILD)/color-discard: tests/color_discard.c tests/consumer.h tests/adapter_class.h $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
$(BUILD)/msaa-sampling: tests/msaa_sampling.c tests/consumer.h tests/adapter_class.h $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
$(BUILD)/format-features: tests/format_features.c tests/consumer.h tests/adapter_class.h $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
$(BUILD)/draw-features: tests/draw_features.c tests/consumer.h tests/adapter_class.h $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
$(BUILD)/copy-layouts: tests/copy_layouts.c tests/consumer.h tests/adapter_class.h $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
$(BUILD)/bc-compression: tests/bc_compression.c tests/consumer.h tests/adapter_class.h $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
$(BUILD)/component-swizzle: tests/component_swizzle.c tests/consumer.h tests/adapter_class.h $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
$(BUILD)/encoder-errors: tests/encoder_errors.c tests/consumer.h tests/adapter_class.h $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
$(BUILD)/core-formats: tests/core_formats.c tests/consumer.h tests/adapter_class.h $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
$(BUILD)/indirect-compute: tests/indirect_compute.c tests/consumer.h tests/adapter_class.h $(OBJECTS) $(COMPILER_LIBRARY) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/indirect_compute.c $(OBJECTS) $(COMPILER_LINK) $(PLATFORM_LIBS) -Wl,--wrap=doe_vk_dispatch_validator_create -o $@
$(BUILD)/compute: tests/compute.c tests/adapter_class.h $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
$(BUILD)/atomics: tests/atomics.c tests/consumer.h tests/adapter_class.h $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
$(BUILD)/debug: tests/debug.c tests/consumer.h tests/adapter_class.h $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
$(BUILD)/msaa: tests/msaa.c tests/consumer.h tests/adapter_class.h $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
$(BUILD)/depth: tests/depth.c tests/consumer.h tests/adapter_class.h $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
$(BUILD)/demote: tests/demote.c tests/consumer.h tests/adapter_class.h $(OBJECTS) $(COMPILER_LIBRARY) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/demote.c $(OBJECTS) $(COMPILER_LINK) $(PLATFORM_LIBS) -Wl,--wrap=doe_vk_context_create -o $@
$(BUILD)/graphics-admission: tests/graphics_admission.c tests/consumer.h tests/adapter_class.h $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
$(BUILD)/graphics: tests/graphics.c tests/consumer.h tests/adapter_class.h $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
$(BUILD)/textures: tests/textures.c tests/consumer.h tests/adapter_class.h $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
$(BUILD)/admission: tests/admission.c tests/consumer.h tests/adapter_class.h $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
$(BUILD)/failures: tests/failures.c tests/consumer.h tests/adapter_class.h $(OBJECTS) $(COMPILER_LIBRARY) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/failures.c $(OBJECTS) $(COMPILER_LINK) $(PLATFORM_LIBS) -Wl,--wrap=doe_vk_buffer_create -Wl,--wrap=doe_vk_pipeline_create -Wl,--wrap=doe_vk_render_pipeline_create -Wl,--wrap=doe_vk_timestamp_normalizer_create -o $@
$(BUILD)/timestamps: tests/timestamps.c tests/consumer.h tests/adapter_class.h $(OBJECTS) $(COMPILER_LIBRARY) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/timestamps.c $(OBJECTS) $(COMPILER_LINK) $(PLATFORM_LIBS) -o $@
$(BUILD)/recovery: tests/recovery.c tests/consumer.h tests/adapter_class.h $(OBJECTS) $(COMPILER_LIBRARY) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/recovery.c $(OBJECTS) $(COMPILER_LINK) $(PLATFORM_LIBS) -Wl,--wrap=doe_vk_commands_submit -Wl,--wrap=doe_vk_commands_poll -Wl,--wrap=doe_vk_commands_recover -Wl,--wrap=doe_vk_commands_release -o $@
# A real loader control: code admission and dlclose lifetime, without a GPU.
ifeq ($(SHARED_PLATFORM),Darwin)
PIN_FIXTURE_FLAGS = -dynamiclib -Wl,-undefined,error
else
PIN_FIXTURE_FLAGS = -shared -Wl,-z,defs
endif
$(BUILD)/provider-pin-fixture.$(SHARED_SUFFIX): tests/provider_pin_fixture.c tests/provider_pin_fixture.h src/provider_pin.c src/provider_pin.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -fvisibility=hidden $(PIN_FIXTURE_FLAGS) tests/provider_pin_fixture.c src/provider_pin.c $(PLATFORM_LIBS) -o $@
$(BUILD)/provider-pin: tests/provider_pin.c tests/provider_pin_fixture.h src/provider_pin.c src/provider_pin.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/provider_pin.c src/provider_pin.c $(PLATFORM_LIBS) -o $@
.PHONY: check-provider-pin
check-provider-pin: $(BUILD)/provider-pin $(BUILD)/provider-pin-fixture.$(SHARED_SUFFIX)
	$(BUILD)/provider-pin $(abspath $(BUILD)/provider-pin-fixture.$(SHARED_SUFFIX))
$(BUILD)/backend-failure: tests/backend_failure.c ../backends/metal/tests/mock_native.c ../backends/metal/tests/mock_native.h $(OBJECTS) $(COMPILER_LIBRARY) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/backend_failure.c ../backends/metal/tests/mock_native.c $(OBJECTS) $(COMPILER_LINK) $(PLATFORM_LIBS) -o $@
$(BUILD)/vulkan-identity: tests/vulkan_identity.c ../backends/vulkan/tests/adapter_identity_fixture.h $(OBJECTS) $(COMPILER_LIBRARY) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/vulkan_identity.c $(OBJECTS) $(COMPILER_LINK) $(PLATFORM_LIBS) \
	  -Wl,--wrap=doe_vk_context_create -Wl,--wrap=doe_vk_context_release -o $@
$(BUILD)/pending-progress: tests/pending_progress.c tests/consumer.h tests/adapter_class.h $(OBJECTS) $(COMPILER_LIBRARY) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/pending_progress.c $(OBJECTS) $(COMPILER_LINK) $(PLATFORM_LIBS) \
	  -Wl,--wrap=pthread_create -Wl,--wrap=native_backend_close -Wl,--wrap=native_buffer_close \
	  -Wl,--wrap=doe_vk_commands_submit -Wl,--wrap=doe_vk_commands_poll -Wl,--wrap=doe_vk_commands_recover -Wl,--wrap=doe_vk_commands_release -o $@
$(BUILD)/map-oom: tests/map_oom.c tests/consumer.h tests/adapter_class.h $(OBJECTS) $(COMPILER_LIBRARY) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(OBJECTS) $(COMPILER_LINK) $(PLATFORM_LIBS) \
	  -Wl,--wrap=malloc -Wl,--wrap=native_buffer_close -Wl,--wrap=native_backend_close -o $@
$(BUILD)/command-resources: tests/command_resources.c tests/consumer.h tests/adapter_class.h $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
$(BUILD)/binding-aliases: tests/binding_aliases.c tests/consumer.h tests/adapter_class.h $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
$(BUILD)/program-prepare: tests/program_prepare.c tests/consumer.h tests/adapter_class.h include/doe_native_programs.h $(OBJECTS) $(COMPILER_LIBRARY) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(OBJECTS) $(COMPILER_LINK) $(PLATFORM_LIBS) \
	  -Wl,--wrap=doe_vk_commands_create_reusable -Wl,--wrap=doe_vk_commands_dispatch_resources -Wl,--wrap=doe_vk_commands_seal \
	  -Wl,--wrap=doe_vk_commands_submit -Wl,--wrap=doe_vk_commands_poll -Wl,--wrap=doe_vk_commands_recover \
	  -Wl,--wrap=doe_vk_commands_release -Wl,--wrap=native_backend_close -Wl,--wrap=calloc -o $@
$(BUILD)/vertex-formats: tests/vertex_formats.c tests/consumer.h tests/adapter_class.h $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
$(BUILD)/compute-programs: tests/compute_programs.c tests/consumer.h tests/adapter_class.h include/doe_native_programs.h $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
$(BUILD)/uniform-standard: tests/uniform_standard.c tests/consumer.h tests/adapter_class.h $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
$(BUILD)/wgsl-language-features: tests/wgsl_language_features.c tests/consumer.h tests/adapter_class.h $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
$(BUILD)/texture-binding-view-dimension: tests/texture_binding_view_dimension.c tests/consumer.h tests/adapter_class.h $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
$(BUILD)/mrt: tests/mrt.c tests/consumer.h tests/adapter_class.h $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
$(BUILD)/large-write: tests/large_write.c tests/consumer.h tests/adapter_class.h $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
$(BUILD)/limits: tests/limits.c tests/consumer.h tests/adapter_class.h $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
$(BUILD)/storage-textures: tests/storage_textures.c tests/consumer.h tests/adapter_class.h $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -lm -o $@
$(BUILD)/anisotropy: tests/anisotropy.c tests/consumer.h tests/adapter_class.h $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
$(BUILD)/empty-shader: tests/empty_shader.c tests/consumer.h tests/adapter_class.h $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
$(BUILD)/lifecycle: tests/lifecycle.c tests/adapter_class.h $(OBJECTS) $(COMPILER_LIBRARY) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/lifecycle.c $(OBJECTS) $(COMPILER_LINK) $(PLATFORM_LIBS) -o $@
$(BUILD)/procedures-cxx: tests/procedures.cpp $(NATIVE_LIBRARY)
	$(CXX) $(CPPFLAGS) -std=c++17 -Wall -Wextra -Wpedantic -Werror $(SANITIZE) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
ifeq ($(SHARED_PLATFORM),Linux)
check: check-d3d12-indirect-layout $(BUILD)/indirect-dispatch $(BUILD)/map-oom $(BUILD)/command-resources $(BUILD)/binding-aliases $(BUILD)/mixed-resources $(BUILD)/offscreen-triangle check-provider-pin $(BUILD)/backend-failure $(BUILD)/vulkan-identity $(BUILD)/draw-budget $(BUILD)/indirect-render $(BUILD)/indexed-render $(BUILD)/depth-sampling $(BUILD)/cube-array $(BUILD)/view-formats $(BUILD)/point-list $(BUILD)/color-discard $(BUILD)/msaa-sampling $(BUILD)/format-features $(BUILD)/draw-features $(BUILD)/copy-layouts $(BUILD)/bc-compression $(BUILD)/component-swizzle $(BUILD)/encoder-errors $(BUILD)/core-formats $(BUILD)/indirect-compute check-build-dependencies check-host-bridge $(BUILD)/atomics $(BUILD)/debug $(BUILD)/msaa $(BUILD)/depth $(BUILD)/demote $(BUILD)/graphics-admission $(BUILD)/empty-shader $(BUILD)/graphics $(BUILD)/compute $(BUILD)/procedures-cxx $(BUILD)/lifecycle $(BUILD)/textures $(BUILD)/admission $(BUILD)/failures $(BUILD)/timestamps $(BUILD)/recovery $(BUILD)/pending-progress $(BUILD)/depth-copies $(BUILD)/uniform-standard $(BUILD)/wgsl-language-features $(BUILD)/texture-binding-view-dimension $(BUILD)/compute-programs $(BUILD)/program-prepare $(BUILD)/vertex-formats $(BUILD)/mrt $(BUILD)/anisotropy $(BUILD)/storage-textures $(BUILD)/limits $(BUILD)/large-write
	$(BUILD)/map-oom
	$(BUILD)/binding-aliases
	$(BUILD)/command-resources
	$(BUILD)/draw-budget
	$(BUILD)/indirect-render
	$(BUILD)/indexed-render
	$(BUILD)/depth-copies
	$(BUILD)/depth-sampling
	$(BUILD)/cube-array
	$(BUILD)/view-formats
	$(BUILD)/point-list
	$(BUILD)/color-discard
	$(BUILD)/msaa-sampling
	$(BUILD)/format-features
	$(BUILD)/draw-features
	$(BUILD)/copy-layouts
	$(BUILD)/bc-compression
	$(BUILD)/component-swizzle
	$(BUILD)/encoder-errors
	$(BUILD)/core-formats
	$(BUILD)/indirect-compute
	$(BUILD)/indirect-dispatch
	$(BUILD)/atomics
	$(BUILD)/debug
	$(BUILD)/msaa
	$(BUILD)/depth
	$(BUILD)/demote
	$(BUILD)/graphics-admission
	$(BUILD)/offscreen-triangle
	$(BUILD)/mixed-resources
	$(BUILD)/empty-shader
	$(BUILD)/graphics
	$(BUILD)/compute
	$(BUILD)/procedures-cxx
	$(BUILD)/lifecycle
	$(BUILD)/textures
	$(BUILD)/admission
	$(BUILD)/failures
	$(BUILD)/timestamps
	$(BUILD)/recovery
	$(BUILD)/pending-progress
	$(BUILD)/vulkan-identity
	$(BUILD)/backend-failure
	$(BUILD)/uniform-standard
	$(BUILD)/wgsl-language-features
	$(BUILD)/compute-programs
	$(BUILD)/program-prepare
	$(BUILD)/vertex-formats
	$(BUILD)/texture-binding-view-dimension
	$(BUILD)/mrt
	$(BUILD)/anisotropy
	$(BUILD)/storage-textures
	$(BUILD)/limits
	$(BUILD)/large-write
	python3 tests/audit-exports.py $(NATIVE_LIBRARY) $(if $(SANITIZE),,--runtime)
# The same public clients on a nonfallback GPU adapter (DOE_C_TEST_ADAPTER=physical).
# Developer evidence on the printed adapter/driver only, not qualification. Uses the
# system ICDs: unset VK_ICD_FILENAMES. Never run on a host under a physical hold.
PHYSICAL_CLIENTS = indirect-dispatch map-oom command-resources binding-aliases mixed-resources offscreen-triangle draw-budget indirect-render indexed-render depth-copies depth-sampling cube-array view-formats point-list color-discard msaa-sampling format-features draw-features copy-layouts bc-compression component-swizzle encoder-errors core-formats indirect-compute atomics debug msaa depth \
	demote graphics-admission empty-shader graphics compute lifecycle textures admission \
	failures timestamps recovery uniform-standard wgsl-language-features texture-binding-view-dimension compute-programs program-prepare vertex-formats mrt anisotropy storage-textures limits large-write
# Real-window presentation (XCB on a reachable X server, e.g. Xwayland); not part
# of headless `check`. Exit 77 means skipped (no display), never a pass.
SURFACE_TEST_HEADERS = tests/surface_consumer.h tests/consumer.h tests/adapter_class.h
$(BUILD)/surface-present: tests/surface_present.c $(SURFACE_TEST_HEADERS) $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -lxcb -o $@
$(BUILD)/surface-platforms: tests/surface_platforms.c $(SURFACE_TEST_HEADERS) $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -lX11 -lwayland-client -o $@
$(BUILD)/surface-progress: tests/surface_progress.c $(SURFACE_TEST_HEADERS) $(OBJECTS) $(COMPILER_LIBRARY) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(OBJECTS) $(COMPILER_LINK) $(PLATFORM_LIBS) \
	  -Wl,--wrap=doe_vk_commands_release -Wl,--wrap=native_backend_close -Wl,--wrap=free -lxcb -o $@
.PHONY: check-surface-progress
check-surface-progress: $(BUILD)/surface-progress
	$(BUILD)/surface-progress
.PHONY: check-present
check-present: $(BUILD)/surface-present $(BUILD)/surface-platforms
	$(BUILD)/surface-present
	$(BUILD)/surface-platforms
.PHONY: check-physical
check-physical: $(addprefix $(BUILD)/,$(PHYSICAL_CLIENTS))
	@set -e; for client in $(PHYSICAL_CLIENTS); do \
		env -u VK_ICD_FILENAMES DOE_C_TEST_ADAPTER=physical $(BUILD)/$$client; \
	done
else
check:
	@echo "The SwiftShader/fault-injection suite requires Linux; use check-portable for host ABI checks." >&2
	@exit 2
GNU_ONLY_TESTS = map-oom metal-bridge demote failures recovery indirect-compute pending-progress vulkan-identity surface-progress program-prepare
$(addprefix $(BUILD)/,$(GNU_ONLY_TESTS)): | unavailable-gnu-tests
.PHONY: unavailable-gnu-tests
unavailable-gnu-tests:
	@echo "This control requires GNU linker --wrap and is not a Darwin qualification substitute." >&2
	@exit 2
endif
.PHONY: check-portable check-platform
check-portable: check-provider-pin $(BUILD)/backend-failure check-build-dependencies $(BUILD)/backend-selection $(BUILD)/procedures-cxx
	$(BUILD)/backend-failure
	$(BUILD)/backend-selection
	$(BUILD)/procedures-cxx
	python3 tests/audit-exports.py $(NATIVE_LIBRARY) $(if $(SANITIZE),,--runtime)
check-platform:
	python3 tests/platform_build.py
check-build-dependencies:
	python3 tests/build_dependencies.py
.PHONY: check-host-bridge
check-host-bridge: $(BUILD)/backend-selection $(BUILD)/metal-bridge
	$(BUILD)/backend-selection
	$(BUILD)/metal-bridge
check-empty-module: $(BUILD)/empty-shader
	$(BUILD)/empty-shader
clean:
	rm -f $(BUILD)/command-resources $(BUILD)/indirect-dispatch
	rm -f $(OBJECTS)
	rm -f $(BUILD)/map-oom $(BUILD)/binding-aliases $(BUILD)/native.exports $(BUILD)/native.exports.tmp $(BUILD)/draw-budget $(BUILD)/indirect-render $(BUILD)/indexed-render $(BUILD)/depth-sampling $(BUILD)/cube-array $(BUILD)/view-formats $(BUILD)/point-list $(BUILD)/color-discard $(BUILD)/msaa-sampling $(BUILD)/format-features $(BUILD)/draw-features $(BUILD)/copy-layouts $(BUILD)/bc-compression $(BUILD)/component-swizzle $(BUILD)/encoder-errors $(BUILD)/core-formats $(BUILD)/indirect-compute $(BUILD)/backend-selection $(BUILD)/metal-bridge $(BUILD)/atomics $(BUILD)/debug $(BUILD)/msaa $(BUILD)/depth $(BUILD)/demote $(BUILD)/graphics-admission $(BUILD)/graphics $(BUILD)/empty-shader $(NATIVE_LIBRARY) $(BUILD)/libdoe_compiler.$(SHARED_SUFFIX) $(BUILD)/compute $(BUILD)/procedures-cxx $(BUILD)/lifecycle $(BUILD)/textures $(BUILD)/admission $(BUILD)/failures $(BUILD)/timestamps $(BUILD)/recovery $(BUILD)/pending-progress $(BUILD)/vulkan-identity $(BUILD)/provider-pin $(BUILD)/provider-pin-fixture.$(SHARED_SUFFIX) $(BUILD)/backend-failure $(BUILD)/depth-copies $(BUILD)/surface-present $(BUILD)/surface-platforms $(BUILD)/uniform-standard $(BUILD)/wgsl-language-features $(BUILD)/texture-binding-view-dimension $(BUILD)/mrt $(BUILD)/anisotropy $(BUILD)/storage-textures $(BUILD)/compute-programs $(BUILD)/program-prepare $(BUILD)/surface-progress $(BUILD)/limits $(BUILD)/large-write $(BUILD)/mixed-resources $(BUILD)/offscreen-triangle

$(BUILD)/offscreen-triangle: tests/offscreen_triangle.c tests/metal_graphics_cases.h tests/consumer.h tests/adapter_class.h $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@

$(BUILD)/mixed-resources: tests/mixed_resources.c tests/mixed_resource_cases.h tests/consumer.h tests/adapter_class.h $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@

$(BUILD)/indirect-dispatch: tests/indirect_dispatch.c tests/consumer.h tests/adapter_class.h $(NATIVE_LIBRARY) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
.PHONY: check-d3d12-indirect-layout
check-d3d12-indirect-layout: $(COMPILER_LIBRARY)
	python3 tools/check_d3d12_indirect.py --compiler-library "$(abspath $(COMPILER_LIBRARY))" --build-root "$(abspath $(BUILD))"
