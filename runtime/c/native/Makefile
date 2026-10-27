CC = gcc
CXX = g++
include $(dir $(lastword $(MAKEFILE_LIST)))../build-root.mk
BUILD ?= $(DOE_C_TMP)/native
include platform.mk
NATIVE_LIBRARY ?= $(BUILD)/$(NATIVE_LIBRARY_BASENAME)
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
$(BUILD)/shader-cache: tests/shader_cache.c src/shader_cache.c src/shader_disk_cache.c src/shader_cache_identity.c src/shader_cache_identity.h src/shader_cache.h src/shader_disk_cache.h ../backends/vulkan/src/cache_file.c $(wildcard ../shader/src/*.c ../shader/src/*.h) ../shader/include/doe_shader.h $(COMPILER_LIBRARY) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/shader_cache.c src/shader_cache.c src/shader_disk_cache.c src/shader_cache_identity.c ../backends/vulkan/src/cache_file.c $(wildcard ../shader/src/*.c) $(COMPILER_LINK) $(PLATFORM_LIBS) -o $@
.PHONY: check-shader-cache
check-shader-cache: $(BUILD)/shader-cache
	@directory=$$(mktemp -d); trap 'rm -f "$$directory"/*.bin; rmdir "$$directory"' 0; DOE_SHADER_CACHE_DIR="$$directory" $(BUILD)/shader-cache
check: check-shader-cache
.PHONY: check-shader-cache-reload
check-shader-cache-reload: $(BUILD)/shader-cache
	@directory=$$(mktemp -d); trap 'rm -f "$$directory"/*.bin; rmdir "$$directory"' 0; \
	DOE_SHADER_CACHE_DIR="$$directory" $(BUILD)/shader-cache warm && DOE_SHADER_CACHE_DIR="$$directory" $(BUILD)/shader-cache reload
$(BUILD)/shader-cache-changed-image: tests/shader_cache.c src/shader_cache.c src/shader_disk_cache.c src/shader_cache_identity.c $(wildcard ../shader/src/*.c ../shader/src/*.h ../shader/include/*.h) $(COMPILER_LIBRARY) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DDOE_CACHE_IDENTITY_VARIANT=1 tests/shader_cache.c src/shader_cache.c src/shader_disk_cache.c src/shader_cache_identity.c ../backends/vulkan/src/cache_file.c $(wildcard ../shader/src/*.c) $(COMPILER_LINK) $(PLATFORM_LIBS) -o $@
ifeq ($(SHARED_PLATFORM),Darwin)
.PHONY: check-shader-cache-macho
check-shader-cache-macho: $(BUILD)/shader-cache $(BUILD)/shader-cache-changed-image
	python3 tests/mac_shader_cache_identity.py --binary "$(BUILD)/shader-cache" --changed-binary "$(BUILD)/shader-cache-changed-image"
check: check-shader-cache-macho
endif
ifeq ($(SHARED_PLATFORM),Linux)
$(BUILD)/shader-cache-no-build-id: tests/shader_cache.c src/shader_cache.c src/shader_disk_cache.c src/shader_cache_identity.c $(wildcard ../shader/src/*.c ../shader/src/*.h ../shader/include/*.h) $(COMPILER_LIBRARY) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/shader_cache.c src/shader_cache.c src/shader_disk_cache.c src/shader_cache_identity.c ../backends/vulkan/src/cache_file.c $(wildcard ../shader/src/*.c) $(COMPILER_LINK) $(PLATFORM_LIBS) -Wl,--build-id=none -o $@
.PHONY: check-shader-cache-no-build-id
check-shader-cache-no-build-id: $(BUILD)/shader-cache-no-build-id
	$(BUILD)/shader-cache-no-build-id identity-absent
.PHONY: check-shader-cache-image-change
check-shader-cache-image-change: $(BUILD)/shader-cache $(BUILD)/shader-cache-changed-image
	@directory=$$(mktemp -d); trap 'rm -f "$$directory"/*.bin; rmdir "$$directory"' 0; \
	DOE_SHADER_CACHE_DIR="$$directory" $(BUILD)/shader-cache warm && DOE_SHADER_CACHE_DIR="$$directory" $(BUILD)/shader-cache-changed-image reload-miss
check: check-shader-cache-reload check-shader-cache-no-build-id check-shader-cache-image-change
endif
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
$(BUILD)/native.exports: EXPORTED_SYMBOLS.txt EXTENSION_SYMBOLS.txt LEGACY_SYMBOLS.txt SHARED_MEMORY_SYMBOLS.txt | $(BUILD)
	cat $^ | sed 's/^/_/' > $@.tmp
	mv $@.tmp $@
$(BUILD)/backend-selection: tests/backend_selection.c tests/consumer.h tests/adapter_class.h $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
$(BUILD)/metal-bridge: tests/metal_bridge.c tests/metal_import_cases.h tests/metal_external_cases.h tests/metal_copy_cases.h tests/metal_view_cases.h tests/metal_f16_cases.h tests/sampling_pair_cases.h tests/pipeline_cache_cases.h tests/shader_fixture.h tests/metal_storage_cases.h tests/metal_draw_cases.h tests/metal_depth_cases.h tests/metal_indirect_cases.h tests/metal_vertex_cases.h tests/metal_vertex_span_cases.h tests/metal_graphics_cases.h tests/mixed_resource_cases.h tests/metal_mixed_host.h tests/consumer.h tests/adapter_class.h $(OBJECTS) $(COMPILER_LIBRARY) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/metal_bridge.c ../backends/metal/tests/mock_native.c $(OBJECTS) $(COMPILER_LINK) $(PLATFORM_LIBS) \
	  -Wl,--wrap=doe_mtl_context_create -Wl,--wrap=doe_mtl_context_device -Wl,--wrap=doe_mtl_commands_create \
	  -Wl,--wrap=doe_mtl_commands_wait -Wl,--wrap=doe_mtl_buffer_create \
	  -Wl,--wrap=doe_vk_texture_format_caps -o $@
# Host-only admission control; staging allocation is injected, no GPU is opened.
$(BUILD)/queue-texture-admission: tests/queue_texture_admission.c ../resources/tests/fixture.h $(HEADERS) $(OBJECTS) $(COMPILER_LIBRARY) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/queue_texture_admission.c $(OBJECTS) $(COMPILER_LINK) $(PLATFORM_LIBS) \
	  -Wl,--wrap=native_device_create_buffer -Wl,--wrap=native_error -Wl,--wrap=wgpuQueueSubmit -o $@
.PHONY: check-queue-texture-admission
check-queue-texture-admission: $(BUILD)/queue-texture-admission
	$(BUILD)/queue-texture-admission
check: check-queue-texture-admission
$(BUILD)/occlusion: tests/occlusion.c tests/consumer.h tests/adapter_class.h $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
.PHONY: check-occlusion
check-occlusion: $(BUILD)/occlusion
	$(BUILD)/occlusion
$(BUILD)/workgroups: tests/workgroups.c tests/consumer.h tests/adapter_class.h $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
.PHONY: check-workgroups
check-workgroups: $(BUILD)/workgroups
	$(BUILD)/workgroups
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
$(BUILD)/readonly-depth-sampling: tests/readonly_depth_sampling.c tests/consumer.h tests/adapter_class.h $(NATIVE_LIBRARY)
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
$(BUILD)/vulkan-creation-errors: tests/vulkan_creation_errors.c tests/consumer.h tests/adapter_class.h $(OBJECTS) $(COMPILER_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(OBJECTS) $(COMPILER_LINK) $(PLATFORM_LIBS) -Wl,--wrap=doe_vk_render_pipeline_create,--wrap=doe_vk_occlusion_pool_create,--wrap=doe_vk_query_pool_create -o $@
$(BUILD)/compilation-diagnostics: tests/compilation_diagnostics.c tests/consumer.h tests/adapter_class.h $(OBJECTS) $(COMPILER_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(OBJECTS) $(COMPILER_LINK) $(PLATFORM_LIBS) -Wl,--wrap=calloc -o $@
$(BUILD)/shader-spans: tests/shader_spans.c tests/consumer.h tests/adapter_class.h $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
$(BUILD)/switch-control: tests/switch_control.c tests/consumer.h tests/adapter_class.h $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
$(BUILD)/mapping-timing: tests/mapping_timing.c tests/consumer.h tests/adapter_class.h $(NATIVE_LIBRARY)
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
$(BUILD)/half-interfaces: tests/half_interfaces.c tests/consumer.h tests/adapter_class.h $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
$(BUILD)/graphics: tests/graphics.c tests/consumer.h tests/adapter_class.h $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
$(BUILD)/textures: tests/textures.c tests/consumer.h tests/adapter_class.h $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
$(BUILD)/admission: tests/admission.c tests/consumer.h tests/adapter_class.h $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
$(BUILD)/failures: tests/failures.c tests/consumer.h tests/adapter_class.h $(OBJECTS) $(COMPILER_LIBRARY) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/failures.c $(OBJECTS) $(COMPILER_LINK) $(PLATFORM_LIBS) -Wl,--wrap=doe_vk_buffer_create -Wl,--wrap=doe_vk_pipeline_create_with_immediates -Wl,--wrap=doe_vk_render_pipeline_create -Wl,--wrap=doe_vk_timestamp_normalizer_create -o $@
$(BUILD)/timestamps: tests/timestamps.c tests/consumer.h tests/adapter_class.h $(OBJECTS) $(COMPILER_LIBRARY) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/timestamps.c $(OBJECTS) $(COMPILER_LINK) $(PLATFORM_LIBS) -o $@
$(BUILD)/recovery: tests/recovery.c tests/consumer.h tests/adapter_class.h $(OBJECTS) $(COMPILER_LIBRARY) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/recovery.c $(OBJECTS) $(COMPILER_LINK) $(PLATFORM_LIBS) -Wl,--wrap=doe_vk_commands_submit -Wl,--wrap=doe_vk_commands_poll -Wl,--wrap=doe_vk_commands_recover -Wl,--wrap=doe_vk_commands_release -Wl,--wrap=doe_vk_commands_wait -o $@
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
$(BUILD)/barrier-coverage: tests/barrier_coverage.c tests/consumer.h tests/adapter_class.h $(OBJECTS) $(COMPILER_LIBRARY) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/barrier_coverage.c $(OBJECTS) $(COMPILER_LINK) $(PLATFORM_LIBS) -o $@
$(BUILD)/async-submit: tests/async_submit.c tests/consumer.h tests/adapter_class.h $(OBJECTS) $(COMPILER_LIBRARY) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/async_submit.c $(OBJECTS) $(COMPILER_LINK) $(PLATFORM_LIBS) -Wl,--wrap=native_buffer_close \
	  -Wl,--wrap=doe_vk_commands_submit -Wl,--wrap=doe_vk_commands_wait -Wl,--wrap=doe_vk_commands_poll -Wl,--wrap=doe_vk_commands_recover -Wl,--wrap=doe_vk_commands_release \
	  -Wl,--wrap=doe_vk_commands_block -o $@
$(BUILD)/queue-sync-info: tests/queue_sync_info.c tests/consumer.h tests/adapter_class.h include/doe_native_queue.h $(OBJECTS) $(COMPILER_LIBRARY) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/queue_sync_info.c $(OBJECTS) $(COMPILER_LINK) $(PLATFORM_LIBS) -Wl,--wrap=doe_vk_commands_poll -o $@
$(BUILD)/fault-sweep: tests/fault_sweep.c tests/consumer.h tests/adapter_class.h $(OBJECTS) $(COMPILER_LIBRARY) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/fault_sweep.c $(OBJECTS) $(COMPILER_LINK) $(PLATFORM_LIBS) -Wl,--wrap=malloc -Wl,--wrap=calloc -o $@
$(BUILD)/pending-progress: tests/pending_progress.c tests/consumer.h tests/adapter_class.h $(OBJECTS) $(COMPILER_LIBRARY) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/pending_progress.c $(OBJECTS) $(COMPILER_LINK) $(PLATFORM_LIBS) \
	  -Wl,--wrap=pthread_create -Wl,--wrap=native_backend_close -Wl,--wrap=native_buffer_close \
	  -Wl,--wrap=doe_vk_commands_submit -Wl,--wrap=doe_vk_commands_poll -Wl,--wrap=doe_vk_commands_recover -Wl,--wrap=doe_vk_commands_release -o $@
$(BUILD)/encoder-finish-oom: tests/encoder_finish_oom.c tests/consumer.h tests/adapter_class.h $(OBJECTS) $(COMPILER_LIBRARY) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(OBJECTS) $(COMPILER_LINK) $(PLATFORM_LIBS) -Wl,--wrap=malloc -o $@
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
$(BUILD)/fused-commands: tests/fused_commands.c tests/consumer.h tests/adapter_class.h include/doe_native_commands.h $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
$(BUILD)/lost-objects: tests/lost_objects.c tests/consumer.h tests/adapter_class.h $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
$(BUILD)/concurrency: tests/concurrency.c tests/consumer.h tests/adapter_class.h $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
$(BUILD)/shader-check: tests/shader_check.c include/doe_native_shader_check.h $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
$(BUILD)/adapter-info: tests/adapter_info.c tests/consumer.h tests/adapter_class.h include/doe_native_adapter.h $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
$(BUILD)/shader-reflection: tests/shader_reflection.c tests/consumer.h tests/adapter_class.h include/doe_native_reflection.h include/doe_native_shader_check.h $(NATIVE_LIBRARY)
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
$(BUILD)/slice-render: tests/slice_render.c tests/consumer.h tests/adapter_class.h $(NATIVE_LIBRARY)
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
$(BUILD)/instance-devices: tests/instance_devices.c tests/adapter_class.h $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
.PHONY: check-instance-devices
check-instance-devices: $(BUILD)/instance-devices $(BUILD)/procedures-cxx
	$(BUILD)/procedures-cxx
	$(BUILD)/instance-devices
ifeq ($(SHARED_PLATFORM),Linux)
check: check-instance-devices
check: $(BUILD)/vulkan-creation-errors $(BUILD)/compilation-diagnostics $(BUILD)/shader-spans $(BUILD)/switch-control $(BUILD)/mapping-timing $(BUILD)/encoder-finish-oom $(BUILD)/device-policy $(BUILD)/pipeline-cache $(BUILD)/draw-guard $(BUILD)/implicit-sampling $(BUILD)/d3d12-graphics check-d3d12-graphics-layout $(BUILD)/depth-comparison $(BUILD)/all-any $(BUILD)/select $(BUILD)/pipeline-overrides check-d3d12-bindings $(BUILD)/texture-bindings $(BUILD)/atomic-operations $(BUILD)/d3d12-texture-desc $(BUILD)/texture-transfers check-d3d12-indirect-layout $(BUILD)/indirect-dispatch $(BUILD)/map-oom $(BUILD)/command-resources $(BUILD)/binding-aliases $(BUILD)/mixed-resources $(BUILD)/offscreen-triangle check-provider-pin $(BUILD)/backend-failure $(BUILD)/vulkan-identity $(BUILD)/draw-budget $(BUILD)/indirect-render $(BUILD)/indexed-render $(BUILD)/depth-sampling $(BUILD)/readonly-depth-sampling $(BUILD)/cube-array $(BUILD)/view-formats $(BUILD)/point-list $(BUILD)/color-discard $(BUILD)/msaa-sampling $(BUILD)/format-features $(BUILD)/draw-features $(BUILD)/copy-layouts $(BUILD)/bc-compression $(BUILD)/component-swizzle $(BUILD)/encoder-errors $(BUILD)/core-formats $(BUILD)/indirect-compute check-build-dependencies check-host-bridge $(BUILD)/atomics $(BUILD)/debug $(BUILD)/msaa $(BUILD)/depth $(BUILD)/demote $(BUILD)/graphics-admission $(BUILD)/empty-shader $(BUILD)/graphics $(BUILD)/compute $(BUILD)/procedures-cxx $(BUILD)/lifecycle $(BUILD)/textures $(BUILD)/admission $(BUILD)/failures $(BUILD)/timestamps $(BUILD)/recovery $(BUILD)/pending-progress $(BUILD)/barrier-coverage $(BUILD)/async-submit $(BUILD)/queue-sync-info $(BUILD)/fault-sweep $(BUILD)/concurrency $(BUILD)/lost-objects $(BUILD)/depth-copies $(BUILD)/uniform-standard $(BUILD)/wgsl-language-features $(BUILD)/texture-binding-view-dimension $(BUILD)/compute-programs $(BUILD)/fused-commands $(BUILD)/shader-check $(BUILD)/shader-reflection $(BUILD)/adapter-info $(BUILD)/program-prepare $(BUILD)/vertex-formats $(BUILD)/mrt $(BUILD)/anisotropy $(BUILD)/storage-textures $(BUILD)/limits $(BUILD)/large-write $(BUILD)/slice-render
	$(BUILD)/vulkan-creation-errors
	$(BUILD)/compilation-diagnostics
	$(BUILD)/shader-spans
	$(BUILD)/switch-control
	$(BUILD)/mapping-timing
	$(BUILD)/encoder-finish-oom
	$(BUILD)/map-oom
	$(BUILD)/binding-aliases
	$(BUILD)/command-resources
	$(BUILD)/draw-budget
	$(BUILD)/indirect-render
	$(BUILD)/indexed-render
	$(BUILD)/depth-copies
	$(BUILD)/depth-sampling
	$(BUILD)/readonly-depth-sampling
	$(BUILD)/depth-comparison
	$(BUILD)/implicit-sampling
	$(BUILD)/draw-guard
	$(BUILD)/d3d12-graphics
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
	$(BUILD)/pipeline-cache
	$(BUILD)/device-policy
	$(BUILD)/encoder-errors
	$(BUILD)/core-formats
	$(BUILD)/indirect-compute
	$(BUILD)/indirect-dispatch
	$(BUILD)/d3d12-texture-desc
	$(BUILD)/texture-bindings
	$(BUILD)/pipeline-overrides
	$(BUILD)/select
	$(BUILD)/all-any
	$(BUILD)/texture-transfers
	$(BUILD)/atomics
	$(BUILD)/atomic-operations ../compiler/tests/atomic_operations.wgsl
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
	$(BUILD)/barrier-coverage
	$(BUILD)/async-submit
	$(BUILD)/queue-sync-info
	$(BUILD)/fault-sweep
	$(BUILD)/concurrency
	$(BUILD)/lost-objects
	$(BUILD)/vulkan-identity
	$(BUILD)/backend-failure
	$(BUILD)/uniform-standard
	$(BUILD)/wgsl-language-features
	$(BUILD)/compute-programs
	$(BUILD)/fused-commands
	$(BUILD)/shader-check
	$(BUILD)/shader-reflection
	$(BUILD)/adapter-info
	$(BUILD)/program-prepare
	$(BUILD)/vertex-formats
	$(BUILD)/texture-binding-view-dimension
	$(BUILD)/mrt
	$(BUILD)/anisotropy
	$(BUILD)/storage-textures
	$(BUILD)/limits
	$(BUILD)/large-write
	$(BUILD)/slice-render
	python3 tests/audit-exports.py $(NATIVE_LIBRARY) $(if $(SANITIZE),,--runtime)
# Stage-4 robustness under sanitizers: concurrency under TSan and ASan, the
# fault sweep under ASan with leak detection. Separate sanitized builds of the
# library; not part of `check`.
SANITIZER_BUILD = $(abspath $(BUILD))/sanitizers
.PHONY: check-sanitizers
check-sanitizers:
	$(MAKE) --no-print-directory BUILD='$(SANITIZER_BUILD)/address' SANITIZE='-fsanitize=address,undefined -fno-omit-frame-pointer' \
	  '$(SANITIZER_BUILD)/address/concurrency' '$(SANITIZER_BUILD)/address/fault-sweep' '$(SANITIZER_BUILD)/address/lost-objects'
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 '$(SANITIZER_BUILD)/address/concurrency'
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 '$(SANITIZER_BUILD)/address/fault-sweep'
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 '$(SANITIZER_BUILD)/address/lost-objects'
	$(MAKE) --no-print-directory BUILD='$(SANITIZER_BUILD)/thread' SANITIZE='-fsanitize=thread -fno-omit-frame-pointer' \
	  '$(SANITIZER_BUILD)/thread/concurrency'
	TSAN_OPTIONS='halt_on_error=1 suppressions=$(abspath tests/tsan.supp)' '$(SANITIZER_BUILD)/thread/concurrency'
# The same public clients on a nonfallback GPU adapter (DOE_C_TEST_ADAPTER=physical).
# Developer evidence on the printed adapter/driver only, not qualification. Uses the
# system ICDs: unset VK_ICD_FILENAMES. Never run on a host under a physical hold.
PHYSICAL_CLIENTS = shader-spans switch-control mapping-timing stage-extensions occlusion workgroups draw-guard implicit-sampling d3d12-graphics d3d12-vertex-inputs depth-comparison all-any select pipeline-overrides texture-bindings atomic-operations texture-transfers indirect-dispatch map-oom command-resources binding-aliases mixed-resources offscreen-triangle draw-budget indirect-render indexed-render depth-copies depth-sampling readonly-depth-sampling cube-array view-formats point-list color-discard msaa-sampling format-features draw-features copy-layouts bc-compression component-swizzle encoder-errors core-formats indirect-compute atomics debug msaa depth \
	demote graphics-admission empty-shader graphics compute lifecycle instance-devices lost-objects textures admission \
	failures timestamps recovery uniform-standard wgsl-language-features texture-binding-view-dimension compute-programs fused-commands program-prepare vertex-formats mrt anisotropy storage-textures limits large-write slice-render
# Real-window presentation (XCB on a reachable X server, e.g. Xwayland); not part
# of headless `check`. Exit 77 means skipped (no display), never a pass.
SURFACE_TEST_HEADERS = tests/surface_consumer.h tests/consumer.h tests/adapter_class.h
# Authorized healthy macOS only; no ordinary check or physical matrix inclusion.
$(BUILD)/metal-surface: tests/metal_surface.c $(SURFACE_TEST_HEADERS) ../runtime-cli/src/surface_metal.m ../runtime-cli/src/surface_platform.h $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) -D_DARWIN_C_SOURCE $(CFLAGS) tests/metal_surface.c ../runtime-cli/src/surface_metal.m \
	  -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -framework AppKit -framework QuartzCore -o $@
.PHONY: check-metal-present
check-metal-present: $(BUILD)/metal-surface
	@test "$$(uname -s)" = Darwin || { echo 'check-metal-present requires an authorized healthy Mac'; exit 1; }
	DOE_C_TEST_ADAPTER=physical DOE_C_TEST_BACKEND=metal python3 -c 'import subprocess; subprocess.run(["$(BUILD)/metal-surface"], check=True, timeout=20)'
$(BUILD)/surface-present: tests/surface_present.c $(SURFACE_TEST_HEADERS) $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -lxcb -o $@
$(BUILD)/surface-platforms: tests/surface_platforms.c $(SURFACE_TEST_HEADERS) $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -lX11 -lwayland-client -o $@
$(BUILD)/surface-progress: tests/surface_progress.c $(SURFACE_TEST_HEADERS) $(OBJECTS) $(COMPILER_LIBRARY) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(OBJECTS) $(COMPILER_LINK) $(PLATFORM_LIBS) \
	  -Wl,--wrap=doe_vk_commands_release -Wl,--wrap=native_backend_close -Wl,--wrap=free -lxcb -o $@
$(BUILD)/surface-faults: tests/surface_faults.c $(SURFACE_TEST_HEADERS) $(OBJECTS) $(COMPILER_LIBRARY) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(OBJECTS) $(COMPILER_LINK) $(PLATFORM_LIBS) -Wl,--wrap=malloc -Wl,--wrap=calloc -lxcb -o $@
.PHONY: check-surface-progress
check-surface-progress: $(BUILD)/surface-progress
	$(BUILD)/surface-progress
.PHONY: check-present
check-present: $(BUILD)/surface-present $(BUILD)/surface-platforms $(BUILD)/surface-faults
	$(BUILD)/surface-present
	$(BUILD)/surface-platforms
	$(BUILD)/surface-faults
# The surface fault sweep and concurrency under ASan+UBSan (leaks) and TSan.
.PHONY: check-present-sanitizers
check-present-sanitizers:
	$(MAKE) --no-print-directory BUILD='$(SANITIZER_BUILD)/address' SANITIZE='-fsanitize=address,undefined -fno-omit-frame-pointer' \
	  '$(SANITIZER_BUILD)/address/surface-faults'
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 '$(SANITIZER_BUILD)/address/surface-faults'
	$(MAKE) --no-print-directory BUILD='$(SANITIZER_BUILD)/thread' SANITIZE='-fsanitize=thread -fno-omit-frame-pointer' \
	  '$(SANITIZER_BUILD)/thread/surface-faults'
	TSAN_OPTIONS='halt_on_error=1 suppressions=$(abspath tests/tsan.supp)' '$(SANITIZER_BUILD)/thread/surface-faults'
.PHONY: check-physical
check-physical: $(addprefix $(BUILD)/,$(PHYSICAL_CLIENTS))
	@set -e; for client in $(PHYSICAL_CLIENTS); do \
		env -u VK_ICD_FILENAMES DOE_C_TEST_ADAPTER=physical $(BUILD)/$$client; \
	done
else
check:
	@echo "The SwiftShader/fault-injection suite requires Linux; use check-portable for host ABI checks." >&2
	@exit 2
GNU_ONLY_TESTS = encoder-finish-oom shared-memory metal-device-policy d3d12-device-policy map-oom fault-sweep metal-bridge demote failures recovery indirect-compute pending-progress vulkan-identity surface-progress program-prepare
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
	rm -f $(BUILD)/d3d12-graphics-vertices $(BUILD)/d3d12-vertex-inputs $(BUILD)/draw-guard $(BUILD)/implicit-sampling $(BUILD)/d3d12-graphics-limits $(BUILD)/d3d12-graphics-layout $(BUILD)/d3d12-graphics $(BUILD)/depth-comparison $(BUILD)/all-any $(BUILD)/select $(BUILD)/pipeline-overrides $(BUILD)/texture-bindings $(BUILD)/command-resources $(BUILD)/indirect-dispatch $(BUILD)/texture-transfers $(BUILD)/d3d12-texture-desc $(BUILD)/atomic-operations
	rm -f $(OBJECTS)
	rm -f $(BUILD)/vulkan-creation-errors $(BUILD)/compilation-diagnostics $(BUILD)/shader-spans $(BUILD)/switch-control $(BUILD)/mapping-timing $(BUILD)/encoder-finish-oom $(BUILD)/map-oom $(BUILD)/fault-sweep $(BUILD)/concurrency $(BUILD)/lost-objects $(BUILD)/binding-aliases $(BUILD)/native.exports $(BUILD)/native.exports.tmp $(BUILD)/draw-budget $(BUILD)/indirect-render $(BUILD)/indexed-render $(BUILD)/depth-sampling $(BUILD)/readonly-depth-sampling $(BUILD)/cube-array $(BUILD)/view-formats $(BUILD)/point-list $(BUILD)/color-discard $(BUILD)/msaa-sampling $(BUILD)/format-features $(BUILD)/draw-features $(BUILD)/copy-layouts $(BUILD)/bc-compression $(BUILD)/component-swizzle $(BUILD)/encoder-errors $(BUILD)/core-formats $(BUILD)/indirect-compute $(BUILD)/backend-selection $(BUILD)/metal-bridge $(BUILD)/atomics $(BUILD)/debug $(BUILD)/msaa $(BUILD)/depth $(BUILD)/demote $(BUILD)/graphics-admission $(BUILD)/graphics $(BUILD)/empty-shader $(NATIVE_LIBRARY) $(BUILD)/libdoe_compiler.$(SHARED_SUFFIX) $(BUILD)/compute $(BUILD)/procedures-cxx $(BUILD)/lifecycle $(BUILD)/textures $(BUILD)/admission $(BUILD)/failures $(BUILD)/timestamps $(BUILD)/recovery $(BUILD)/pending-progress $(BUILD)/vulkan-identity $(BUILD)/provider-pin $(BUILD)/provider-pin-fixture.$(SHARED_SUFFIX) $(BUILD)/backend-failure $(BUILD)/depth-copies $(BUILD)/surface-present $(BUILD)/surface-platforms $(BUILD)/surface-faults $(BUILD)/uniform-standard $(BUILD)/wgsl-language-features $(BUILD)/texture-binding-view-dimension $(BUILD)/mrt $(BUILD)/anisotropy $(BUILD)/storage-textures $(BUILD)/compute-programs $(BUILD)/fused-commands $(BUILD)/shader-check $(BUILD)/shader-reflection $(BUILD)/adapter-info $(BUILD)/program-prepare $(BUILD)/surface-progress $(BUILD)/limits $(BUILD)/large-write $(BUILD)/slice-render $(BUILD)/mixed-resources $(BUILD)/offscreen-triangle

$(BUILD)/offscreen-triangle: tests/offscreen_triangle.c tests/metal_graphics_cases.h tests/consumer.h tests/adapter_class.h $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@

$(BUILD)/mixed-resources: tests/mixed_resources.c tests/mixed_resource_cases.h tests/consumer.h tests/adapter_class.h $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@

$(BUILD)/indirect-dispatch: tests/indirect_dispatch.c tests/consumer.h tests/adapter_class.h $(NATIVE_LIBRARY) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
.PHONY: check-d3d12-indirect-layout
check-d3d12-indirect-layout: $(COMPILER_LIBRARY)
	python3 tools/check_d3d12_indirect.py --compiler-library "$(abspath $(COMPILER_LIBRARY))" --build-root "$(abspath $(BUILD))"

$(BUILD)/d3d12-texture-desc: tests/d3d12_texture_desc.c src/d3d12_texture_desc.c src/d3d12_texture_desc.h $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/d3d12_texture_desc.c src/d3d12_texture_desc.c -o $@
$(BUILD)/texture-transfers: tests/texture_transfers.c tests/consumer.h tests/adapter_class.h $(NATIVE_LIBRARY) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@

$(BUILD)/atomic-operations: tests/atomic_operations.c tests/shader_fixture.h tests/consumer.h tests/adapter_class.h ../compiler/tests/atomic_operations.wgsl $(NATIVE_LIBRARY) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@

.PHONY: check-d3d12-bindings
check-d3d12-bindings: $(COMPILER_LIBRARY)
	python3 tools/check_d3d12_bindings.py --compiler-library "$(abspath $(COMPILER_LIBRARY))" --build-root "$(abspath $(BUILD))"
$(BUILD)/texture-bindings: tests/texture_bindings.c tests/sampling_pair_cases.h tests/consumer.h tests/adapter_class.h $(NATIVE_LIBRARY) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@

$(BUILD)/pipeline-overrides: tests/pipeline_overrides.c tests/consumer.h tests/adapter_class.h $(NATIVE_LIBRARY) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@

$(BUILD)/all-any: tests/all_any.c tests/shader_fixture.h tests/consumer.h tests/adapter_class.h ../compiler/tests/all_any_retained.wgsl ../compiler/tests/all_any_cases.wgsl $(NATIVE_LIBRARY) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@

$(BUILD)/select: tests/select.c tests/shader_fixture.h tests/consumer.h tests/adapter_class.h ../compiler/tests/select_retained.wgsl ../compiler/tests/select_cases.wgsl $(NATIVE_LIBRARY) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@

$(BUILD)/float-math: tests/float_math.c tests/consumer.h tests/adapter_class.h ../compiler/tests/float_math_fixture.h $(NATIVE_LIBRARY) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
.PHONY: check-float-math
check-float-math: $(BUILD)/float-math
	$(BUILD)/float-math

$(BUILD)/depth-comparison: tests/depth_comparison.c tests/shader_fixture.h tests/consumer.h tests/adapter_class.h ../compiler/tests/depth_sampling.wgsl $(NATIVE_LIBRARY) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@

# Independent VS/PS module ordinals are projected into the canonical binding union.
$(BUILD)/d3d12-graphics-layout: tests/d3d12_graphics_layout.c src/d3d12_graphics_bindings.c src/d3d12_stage_bindings.c $(wildcard ../shader/src/*.c) $(HEADERS) $(COMPILER_LIBRARY) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/d3d12_graphics_layout.c src/d3d12_graphics_bindings.c src/d3d12_stage_bindings.c $(wildcard ../shader/src/*.c) $(COMPILER_LINK) -o $@
.PHONY: check-d3d12-graphics-layout
check-d3d12-graphics-layout: $(BUILD)/d3d12-graphics-layout $(BUILD)/d3d12-graphics-limits
	$(BUILD)/d3d12-graphics-layout
	$(BUILD)/d3d12-graphics-limits

$(BUILD)/d3d12-graphics-limits: tests/d3d12_graphics_limits.c ../resources/tests/fixture.h $(OBJECTS) $(COMPILER_LIBRARY) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(OBJECTS) $(COMPILER_LINK) $(PLATFORM_LIBS) -o $@

$(BUILD)/d3d12-graphics: tests/d3d12_graphics.c tests/consumer.h tests/adapter_class.h $(NATIVE_LIBRARY) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@

$(BUILD)/implicit-sampling: tests/implicit_sampling.c tests/shader_fixture.h tests/consumer.h tests/adapter_class.h ../compiler/tests/implicit_sampling.wgsl $(NATIVE_LIBRARY) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@

$(BUILD)/draw-guard: tests/draw_guard.c tests/consumer.h tests/adapter_class.h ../common/include/doe_draw_guard.h $(NATIVE_LIBRARY) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@

$(BUILD)/d3d12-graphics-vertices: tests/d3d12_graphics_vertices.c tests/d3d12_vertex_cases.h src/d3d12_graphics_vertices.c src/render_formats.c ../resources/src/formats.c ../resources/src/render_targets.c $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/d3d12_graphics_vertices.c src/d3d12_graphics_vertices.c src/render_formats.c ../resources/src/formats.c ../resources/src/render_targets.c -lm -o $@
.PHONY: check-d3d12-graphics-vertices
check-d3d12-graphics-vertices: $(BUILD)/d3d12-graphics-vertices
	$(BUILD)/d3d12-graphics-vertices
check: check-d3d12-graphics-vertices

$(BUILD)/d3d12-vertex-inputs: tests/d3d12_vertex_inputs.c tests/d3d12_vertex_cases.h tests/consumer.h tests/adapter_class.h $(NATIVE_LIBRARY) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@

$(BUILD)/sample-mask: tests/sample_mask.c tests/consumer.h tests/adapter_class.h $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@

$(BUILD)/bitfield: tests/bitfield.c tests/consumer.h tests/adapter_class.h ../compiler/tests/bitfield_fixture.h $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
.PHONY: check-bitfield
check-bitfield: $(BUILD)/bitfield
	$(BUILD)/bitfield

$(BUILD)/pipeline-cache: tests/pipeline_cache.c tests/pipeline_cache_cases.h tests/consumer.h tests/adapter_class.h $(NATIVE_LIBRARY) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@

$(BUILD)/device-policy: tests/device_policy.c tests/consumer.h tests/adapter_class.h include/doe_native_device_policy.h $(NATIVE_LIBRARY) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@

$(BUILD)/immediates: tests/immediates.c tests/consumer.h tests/adapter_class.h $(NATIVE_LIBRARY) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
.PHONY: check-immediates
check-immediates: $(BUILD)/immediates
	$(BUILD)/immediates

$(BUILD)/metal-device-policy: tests/metal_device_policy.c tests/consumer.h tests/adapter_class.h ../backends/metal/tests/mock_native.c ../backends/metal/tests/mock_native.h $(OBJECTS) $(COMPILER_LIBRARY) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/metal_device_policy.c ../backends/metal/tests/mock_native.c $(OBJECTS) $(COMPILER_LINK) $(PLATFORM_LIBS) \
	  -Wl,--wrap=doe_mtl_context_create -Wl,--wrap=doe_mtl_context_device -Wl,--wrap=doe_mtl_commands_wait -o $@
.PHONY: check-metal-device-policy
check-metal-device-policy: $(BUILD)/metal-device-policy
	$(BUILD)/metal-device-policy

$(BUILD)/d3d12-device-policy: tests/d3d12_device_policy.c $(OBJECTS) $(COMPILER_LIBRARY) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/d3d12_device_policy.c $(OBJECTS) $(COMPILER_LINK) $(PLATFORM_LIBS) \
	  -Wl,--wrap=native_d3d12_reopen -o $@
.PHONY: check-d3d12-device-policy
check-d3d12-device-policy: $(BUILD)/d3d12-device-policy
	$(BUILD)/d3d12-device-policy

$(BUILD)/capability-admission: tests/capability_admission.c $(OBJECTS) $(COMPILER_LIBRARY) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(OBJECTS) $(COMPILER_LINK) $(PLATFORM_LIBS) -o $@
.PHONY: check-capability-admission
check-capability-admission: $(BUILD)/capability-admission
	$(BUILD)/capability-admission

$(BUILD)/shader-source-span: tests/shader_source_span.c tests/consumer.h tests/adapter_class.h $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@

$(BUILD)/stage-extensions: tests/stage_extensions.c tests/consumer.h tests/adapter_class.h $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@

$(BUILD)/external-textures: tests/external_textures.c tests/consumer.h tests/adapter_class.h include/doe_native_external_texture.h $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
.PHONY: check-external-textures
check-external-textures: $(BUILD)/external-textures
	$(BUILD)/external-textures
check: check-external-textures
PHYSICAL_CLIENTS += external-textures

.PHONY: check-metal-external
check-metal-external: $(BUILD)/metal-bridge
	$(BUILD)/metal-bridge --external-only

$(BUILD)/shader-imports: tests/shader_imports.c tests/consumer.h tests/adapter_class.h ../compiler/tests/hlsl_fixture.h $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c $(COMPILER_LINK) -o $@
.PHONY: check-shader-imports
check-shader-imports: $(BUILD)/shader-imports
	$(BUILD)/shader-imports

$(BUILD)/optional-formats: tests/optional_formats.c tests/consumer.h tests/adapter_class.h $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
.PHONY: check-optional-formats
check-optional-formats: $(BUILD)/optional-formats
	$(BUILD)/optional-formats
check: check-optional-formats
PHYSICAL_CLIENTS += optional-formats

$(BUILD)/legacy-api: tests/legacy_api.c tests/consumer.h tests/adapter_class.h include/doe_native_legacy.h $(NATIVE_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
.PHONY: check-legacy-api check-browser-procedures
check-legacy-api: $(BUILD)/legacy-api
	$(BUILD)/legacy-api
check-browser-procedures: $(NATIVE_LIBRARY)
	python3 tests/browser_required_procedures.py $(NATIVE_LIBRARY)
check: check-legacy-api check-browser-procedures
PHYSICAL_CLIENTS += legacy-api

$(BUILD)/shared-memory: tests/shared_memory.c tests/consumer.h tests/adapter_class.h $(OBJECTS) $(COMPILER_LIBRARY) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/shared_memory.c ../backends/metal/tests/mock_native.c $(OBJECTS) $(COMPILER_LINK) $(PLATFORM_LIBS) \
	  -Wl,--wrap=doe_mtl_context_create -Wl,--wrap=doe_mtl_context_device -Wl,--wrap=doe_mtl_commands_wait -o $@
.PHONY: check-shared-memory
check-shared-memory: $(BUILD)/shared-memory
	$(BUILD)/shared-memory
check: check-shared-memory

# Opt-in paired-provider diagnostic. A pixel mismatch remains a failing exit;
# this target does not replace or change check's frozen MSAA consumer.
MSAA_DIAGNOSTIC_LIBRARY ?= $(NATIVE_LIBRARY)
$(BUILD)/msaa-scissor-diagnostic: tests/msaa_scissor_diagnostic.c tests/consumer.h tests/adapter_class.h $(MSAA_DIAGNOSTIC_LIBRARY) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< "$(abspath $(MSAA_DIAGNOSTIC_LIBRARY))" \
	  -Wl,-rpath,$(abspath $(dir $(MSAA_DIAGNOSTIC_LIBRARY))) -o $@
.PHONY: check-msaa-scissor-diagnostic
check-msaa-scissor-diagnostic: $(BUILD)/msaa-scissor-diagnostic
	$(BUILD)/msaa-scissor-diagnostic
