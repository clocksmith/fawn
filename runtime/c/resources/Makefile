CC = gcc
AR = ar
BUILD ?= /tmp/doe-c-resources
CPPFLAGS += -Iinclude -I../instance/include -I../futures/include -I../labels/include -I../../zig/vendor/webgpu-headers
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Werror -pthread
SOURCES = src/objects.c src/formats.c src/textures.c src/texture_copy.c src/bindings.c src/usage_scope.c src/commands.c src/queries.c src/external_textures.c src/render.c src/render_state.c src/render_usage.c src/bundles.c src/compute_pass.c src/accessors.c
OBJECTS = $(patsubst src/%.c,$(BUILD)/%.o,$(SOURCES))
LABEL_SOURCES = $(wildcard ../labels/src/*.c)
.PHONY: all check
all: $(BUILD)/libdoe_resources.a
$(BUILD):
	mkdir -p $@
$(BUILD)/%.o: src/%.c src/internal.h include/doe_resources.h include/doe_resources_extended.h ../common/include/doe_copy_footprint.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/libdoe_resources.a: $(OBJECTS)
	$(AR) rcs $@ $^
$(BUILD)/test_resources: tests/test_resources.c $(BUILD)/libdoe_resources.a $(LABEL_SOURCES) tests/fixture.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter-out %.h,$^) -o $@
$(BUILD)/test_extended: tests/test_extended.c $(BUILD)/libdoe_resources.a $(LABEL_SOURCES) tests/fixture.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter-out %.h,$^) -o $@
$(BUILD)/test_bundles: tests/test_bundles.c $(BUILD)/libdoe_resources.a $(LABEL_SOURCES) tests/fixture.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter-out %.h,$^) -o $@
$(BUILD)/test_binding_features: tests/test_binding_features.c $(BUILD)/libdoe_resources.a $(LABEL_SOURCES) tests/fixture.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter-out %.h,$^) -o $@
$(BUILD)/test_usage_scope: tests/test_usage_scope.c $(BUILD)/libdoe_resources.a $(LABEL_SOURCES) tests/fixture.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter-out %.h,$^) -o $@
$(BUILD)/test_render_usage: tests/test_render_usage.c $(BUILD)/libdoe_resources.a $(LABEL_SOURCES) tests/fixture.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter-out %.h,$^) -o $@
$(BUILD)/test_query_admission: tests/test_query_admission.c $(BUILD)/libdoe_resources.a $(LABEL_SOURCES) tests/fixture.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter-out %.h,$^) -o $@
check: $(BUILD)/test_query_admission $(BUILD)/test_render_usage $(BUILD)/test_usage_scope $(BUILD)/test_resources $(BUILD)/test_extended $(BUILD)/test_bundles $(BUILD)/test_binding_features
	$(BUILD)/test_resources
	$(BUILD)/test_extended
	$(BUILD)/test_bundles
	$(BUILD)/test_binding_features
	$(BUILD)/test_usage_scope
	$(BUILD)/test_render_usage
	$(BUILD)/test_query_admission

VK_BUILD ?= /tmp/doe-c-resources-vulkan
.PHONY: vulkan-lifetime vulkan-library
vulkan-library:
	$(MAKE) -C ../backends/vulkan OUT=$(VK_BUILD) all
$(BUILD)/vulkan_lifetime: tests/vulkan_lifetime.c $(BUILD)/libdoe_resources.a $(LABEL_SOURCES) tests/vulkan_fixture.h vulkan-library
	$(CC) $(CPPFLAGS) -I../backends/vulkan/include $(CFLAGS) $(filter-out %.h vulkan-library,$^) $(VK_BUILD)/libdoe_vk.a -ldl -o $@
vulkan-lifetime: $(BUILD)/vulkan_lifetime
	$(BUILD)/vulkan_lifetime --allow-software

TINT_GRAPHICS ?= ../../../examples/tint-compiler-evidence.benchmark-corpus.spirv.sample.artifacts/particles.wgsl/tint
.PHONY: vulkan-render-lifetime
$(BUILD)/vulkan_render_lifetime: tests/vulkan_render_lifetime.c $(BUILD)/libdoe_resources.a $(LABEL_SOURCES) tests/vulkan_fixture.h vulkan-library
	$(CC) $(CPPFLAGS) -I../backends/vulkan/include $(CFLAGS) $(filter-out %.h vulkan-library,$^) $(VK_BUILD)/libdoe_vk.a -ldl -o $@
vulkan-render-lifetime: $(BUILD)/vulkan_render_lifetime
	$(BUILD)/vulkan_render_lifetime $(TINT_GRAPHICS)/output.vs_main.spv $(TINT_GRAPHICS)/output.fs_main.spv --allow-software

.PHONY: vulkan-query-lifetime
$(BUILD)/vulkan_query_lifetime: tests/vulkan_query_lifetime.c $(BUILD)/libdoe_resources.a $(LABEL_SOURCES) tests/vulkan_fixture.h vulkan-library
	$(CC) $(CPPFLAGS) -I../backends/vulkan/include $(CFLAGS) $(filter-out %.h vulkan-library,$^) $(VK_BUILD)/libdoe_vk.a -ldl -o $@
vulkan-query-lifetime: $(BUILD)/vulkan_query_lifetime
	$(BUILD)/vulkan_query_lifetime --allow-software
