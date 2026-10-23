# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Doe contributors
CC = gcc
CXX = g++
SHARED_LIBRARY ?= build/libdoe_compiler.so
SHARED_PLATFORM ?= $(shell uname -s)
SHARED_INSTALL_NAME ?= @rpath/$(notdir $(SHARED_LIBRARY))
ifeq ($(SHARED_PLATFORM),Darwin)
SHARED_FLAGS = -dynamiclib -Wl,-install_name,$(SHARED_INSTALL_NAME) -Wl,-exported_symbols_list,$(SHARED_LIBRARY).exports
SHARED_EXPORTS = $(SHARED_LIBRARY).exports
else
SHARED_FLAGS = -shared -Wl,--version-script=src/exports.map
SHARED_EXPORTS =
endif
EXTRA_CFLAGS ?=
export TMPDIR := $(CURDIR)/build/tmp
export PYTHONDONTWRITEBYTECODE := 1
CFLAGS = -std=c11 -O2 -fno-fast-math -ffp-contract=off -frounding-math -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wswitch-enum -Werror
CXXFLAGS = -std=c++17 -fno-exceptions -fno-rtti -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
include targets/metal/sources.mk
include targets/hlsl/sources.mk
LIB_SOURCES = $(addprefix targets/metal/,$(METAL_SOURCE_NAMES)) $(addprefix targets/hlsl/,$(HLSL_SOURCE_NAMES)) src/pipeline.c src/compile_spans.c src/compile_metadata.c src/compile_text.c src/text_metal.c src/text_hlsl.c src/sema_matrix.c src/spirv_matrix.c src/spirv_atomic.c src/spirv_uniform.c src/spirv_workgroup.c src/sema_array.c src/sema_atomic.c src/sema_pointer.c src/stage.c src/parse_interface.c src/sema_stage.c src/spirv_stage.c src/sema_finalize.c src/image.c src/sema_image.c src/spirv_image.c src/module.c src/builtin.c src/numeric.c src/numeric_math.c src/numeric_environment.c src/lex.c src/parse.c src/validate.c src/uniformity.c src/types.c src/constants.c src/sema_declarations.c src/sema_expression.c src/sema_call.c src/reflection.c src/spirv.c src/spirv_module.c src/spirv_subgroup.c src/compile.c src/storage.c
HEADERS = include/doe_compiler_hlsl.h src/numeric_host.h include/doe_compiler_text.h src/compile_internal.h src/text_internal.h $(wildcard targets/metal/*.h targets/hlsl/*.h targets/*.h) include/doe_compiler_stages.h src/stage.h src/parse_internal.h include/doe_compiler_resources.h include/doe_compiler_features.h include/doe_compiler_module.h src/builtin.h src/spirv_decl.h src/spirv_internal.h include/doe_compiler_typed.h src/sema.h src/numeric.h src/internal.h include/doe_compiler.h include/doe_compiler_v2.h include/doe_compiler_job.h src/cli_json.h
SPIRV_VAL ?= spirv-val
CLI_FAULT_FLAGS = -Wl,--wrap=write,--wrap=close,--wrap=link,--wrap=mkstemp,--wrap=malloc,--wrap=free,--wrap=doe_compiler_job_requirements
.PHONY: all test test-cli-modes test-uniform sanitize sanitize-cli sanitize-uniform clean
all: build/doe-compiler-c build/libdoe_compiler.so
build/test_array_length_api: $(LIB_SOURCES) tests/test_array_length_api.c $(HEADERS) | build/.ready
	$(CC) $(CFLAGS) $(LIB_SOURCES) tests/test_array_length_api.c -lm -o $@
.PHONY: test-array-length
test-array-length: all build/test_array_length_api
	build/test_array_length_api
	DOE_COMPILER_CLI="$(CURDIR)/build/doe-compiler-c" SPIRV_VAL="$(SPIRV_VAL)" python3 tests/test_array_length.py
build/.ready:
	mkdir -p build/tmp build/logs
	touch $@
build/doe-compiler-c: $(LIB_SOURCES) src/cli.c src/cli_json.c $(HEADERS) | build/.ready
	$(CC) $(CFLAGS) $(LIB_SOURCES) src/cli.c src/cli_json.c -lm -o $@
build/test_cli_faults: $(LIB_SOURCES) src/cli.c src/cli_json.c tests/cli_io_faults.c $(HEADERS) | build/.ready
	$(CC) $(CFLAGS) $(LIB_SOURCES) src/cli.c src/cli_json.c tests/cli_io_faults.c $(CLI_FAULT_FLAGS) -lm -o $@
test-cli-modes: all build/test_cli_faults
	cd ../../.. && SPIRV_VAL="$(SPIRV_VAL)" python3 -m unittest runtime.c.compiler.tests.test_cli_modes
$(SHARED_LIBRARY): $(SHARED_EXPORTS) $(LIB_SOURCES) $(HEADERS) targets/metal/sources.mk targets/hlsl/sources.mk src/exports.map
	mkdir -p $(dir $@) $(TMPDIR)
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -fPIC $(SHARED_FLAGS) $(LIB_SOURCES) -lm -o $@
$(SHARED_LIBRARY).exports: src/exports.map tools/export_symbols.py
	mkdir -p $(dir $@)
	python3 tools/export_symbols.py src/exports.map > $@.tmp
	mv $@.tmp $@
.PHONY: shared
shared: $(SHARED_LIBRARY)
build/test_api: $(LIB_SOURCES) tests/test_api.c $(HEADERS) | build/.ready
	$(CC) $(CFLAGS) $(LIB_SOURCES) tests/test_api.c -lm -o $@
build/test_boundaries: $(LIB_SOURCES) tests/test_boundaries.c $(HEADERS) | build/.ready
	$(CC) $(CFLAGS) -pthread $(LIB_SOURCES) tests/test_boundaries.c -lm -o $@
build/test_constants: $(LIB_SOURCES) tests/test_constants.c $(HEADERS) | build/.ready
	$(CC) $(CFLAGS) $(LIB_SOURCES) tests/test_constants.c -lm -o $@
build/test_expression_api: $(LIB_SOURCES) tests/test_expression_api.c $(HEADERS) | build/.ready
	$(CC) $(CFLAGS) -pthread $(LIB_SOURCES) tests/test_expression_api.c -lm -lm -o $@
build/test_control_api: $(LIB_SOURCES) tests/test_control_api.c $(HEADERS) | build/.ready
	$(CC) $(CFLAGS) -pthread $(LIB_SOURCES) tests/test_control_api.c -lm -o $@
build/native_consumer: tests/native_consumer.c include/doe_compiler.h build/libdoe_compiler.so | build/.ready
	$(CC) $(CFLAGS) tests/native_consumer.c -Lbuild -ldoe_compiler -Wl,-rpath,'$$ORIGIN' -lm -o $@
build/test_layout: tests/test_layout.c include/doe_compiler.h | build/.ready
	$(CC) $(CFLAGS) tests/test_layout.c -lm -o $@
build/cpp_consumer: tests/cpp_consumer.cpp include/doe_compiler.h build/libdoe_compiler.so | build/.ready
	$(CXX) $(CXXFLAGS) tests/cpp_consumer.cpp -Lbuild -ldoe_compiler -Wl,-rpath,'$$ORIGIN' -lm -o $@
build/interposition_consumer: tests/interposition_consumer.c include/doe_compiler.h build/libdoe_compiler.so | build/.ready
	$(CC) $(CFLAGS) tests/interposition_consumer.c -Lbuild -ldoe_compiler -Wl,-rpath,'$$ORIGIN' -Wl,--export-dynamic -lm -o $@
test: all test-job test-compute test-uniform test-cli-modes build/interposition_consumer build/test_api build/test_boundaries build/test_constants build/test_control_api build/test_expression_api build/native_consumer build/test_layout build/cpp_consumer
	build/test_api
	build/test_boundaries
	build/test_constants
	build/test_control_api
	build/test_expression_api
	build/test_layout
	build/cpp_consumer
	build/interposition_consumer
	python3 tests/test_exports.py
	build/native_consumer ../../../bench/inference-pipeline/kernels/residual.wgsl
	SPIRV_VAL="$(SPIRV_VAL)" python3 tests/test_cli.py
	SPIRV_VAL="$(SPIRV_VAL)" python3 tests/test_control.py
	SPIRV_VAL="$(SPIRV_VAL)" python3 tests/test_expressions.py
sanitize: | build/.ready
	$(CC) $(CFLAGS) -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined $(LIB_SOURCES) tests/test_api.c -lm -o build/test_api_sanitize
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 build/test_api_sanitize
	$(CC) $(CFLAGS) -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -pthread $(LIB_SOURCES) tests/test_boundaries.c -lm -o build/test_boundaries_sanitize
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 build/test_boundaries_sanitize
	$(CC) $(CFLAGS) -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined $(LIB_SOURCES) tests/test_constants.c -lm -o build/test_constants_sanitize
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 build/test_constants_sanitize
	$(CC) $(CFLAGS) -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -pthread $(LIB_SOURCES) tests/test_control_api.c -lm -o build/test_control_api_sanitize
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 build/test_control_api_sanitize
	$(CC) $(CFLAGS) -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -pthread $(LIB_SOURCES) tests/test_expression_api.c -lm -lm -o build/test_expression_api_sanitize
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 build/test_expression_api_sanitize
sanitize-cli: | build/.ready
	$(CC) $(CFLAGS) -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined $(LIB_SOURCES) src/cli.c src/cli_json.c -lm -o build/doe-compiler-c-sanitize
	$(CC) $(CFLAGS) -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined $(LIB_SOURCES) src/cli.c src/cli_json.c tests/cli_io_faults.c $(CLI_FAULT_FLAGS) -lm -o build/test_cli_faults_sanitize
	cd ../../.. && ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 DOE_COMPILER_CLI="$(CURDIR)/build/doe-compiler-c-sanitize" DOE_COMPILER_FAULT_CLI="$(CURDIR)/build/test_cli_faults_sanitize" SPIRV_VAL="$(SPIRV_VAL)" python3 -m unittest runtime.c.compiler.tests.test_cli_modes
clean:
	rm -rf build

build/test_uniform_api: $(LIB_SOURCES) tests/test_uniform_api.c $(HEADERS) | build/.ready
	$(CC) $(CFLAGS) -pthread $(LIB_SOURCES) tests/test_uniform_api.c -lm -o $@
build/cpp_consumer_v2: tests/cpp_consumer_v2.cpp include/doe_compiler_v2.h build/libdoe_compiler.so | build/.ready
	$(CXX) $(CXXFLAGS) tests/cpp_consumer_v2.cpp -Lbuild -ldoe_compiler -Wl,-rpath,'$$ORIGIN' -lm -o $@
test-uniform: all build/test_uniform_api build/cpp_consumer_v2 build/native_consumer_v2 build/test_layout_v2
	build/test_uniform_api
	build/cpp_consumer_v2
	build/test_layout_v2
	build/native_consumer_v2 ../../../bench/kernels/package_vector_scale_add.wgsl
	SPIRV_VAL="$(SPIRV_VAL)" python3 tests/test_uniforms.py
sanitize-uniform: | build/.ready
	$(CC) $(CFLAGS) -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -pthread $(LIB_SOURCES) tests/test_uniform_api.c -lm -o build/test_uniform_api_sanitize
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 build/test_uniform_api_sanitize

build/native_consumer_v2: tests/native_consumer_v2.c include/doe_compiler_v2.h build/libdoe_compiler.so | build/.ready
	$(CC) $(CFLAGS) tests/native_consumer_v2.c -Lbuild -ldoe_compiler -Wl,-rpath,'$$ORIGIN' -lm -o $@
build/test_layout_v2: tests/test_layout_v2.c include/doe_compiler_v2.h | build/.ready
	$(CC) $(CFLAGS) tests/test_layout_v2.c -lm -o $@

.PHONY: test-compute
test-compute: all build/test_compute_api
	build/test_compute_api
	cd ../../.. && SPIRV_VAL="$(SPIRV_VAL)" python3 -m unittest runtime.c.compiler.tests.test_compute

build/test_compute_api: $(LIB_SOURCES) tests/test_compute_api.c $(HEADERS) | build/.ready
	$(CC) $(CFLAGS) -pthread $(LIB_SOURCES) tests/test_compute_api.c -lm -o $@
.PHONY: sanitize-compute
sanitize-compute: | build/.ready
	$(CC) $(CFLAGS) -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -pthread $(LIB_SOURCES) tests/test_compute_api.c -lm -o build/test_compute_api_sanitize
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 build/test_compute_api_sanitize

	$(CC) $(CFLAGS) -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined $(LIB_SOURCES) src/cli.c src/cli_json.c -lm -o build/doe-compiler-c-compute-sanitize
	cd ../../.. && ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 DOE_COMPILER_CLI="$(CURDIR)/build/doe-compiler-c-compute-sanitize" SPIRV_VAL="$(SPIRV_VAL)" python3 -m unittest runtime.c.compiler.tests.test_compute

.PHONY: test-job sanitize-job
build/test_job_api: $(LIB_SOURCES) tests/test_job_api.c $(HEADERS) | build/.ready
	$(CC) $(CFLAGS) -pthread $(LIB_SOURCES) tests/test_job_api.c -lm -o $@
test-job: all build/test_job_api build/cpp_consumer_job
	build/cpp_consumer_job
	build/test_job_api
	SPIRV_VAL="$(SPIRV_VAL)" python3 tests/test_job.py
sanitize-job: | build/.ready
	$(CC) $(CFLAGS) -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -pthread $(LIB_SOURCES) tests/test_job_api.c -lm -o build/test_job_api_sanitize
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 build/test_job_api_sanitize
build/cpp_consumer_job: tests/cpp_consumer_job.cpp include/doe_compiler_job.h build/libdoe_compiler.so | build/.ready
	$(CXX) $(CXXFLAGS) tests/cpp_consumer_job.cpp -Lbuild -ldoe_compiler -Wl,-rpath,'$$ORIGIN' -lm -o $@

.PHONY: test-scalar sanitize-scalar
build/test_scalar_api: $(LIB_SOURCES) tests/test_scalar_api.c $(HEADERS) | build/.ready
	$(CC) $(CFLAGS) $(LIB_SOURCES) tests/test_scalar_api.c -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free -lm -o $@
build/test_numeric_values: src/numeric.c src/numeric_math.c src/numeric_environment.c tests/test_numeric_values.c $(HEADERS) | build/.ready
	$(CC) $(CFLAGS) -pthread src/numeric.c src/numeric_math.c src/numeric_environment.c tests/test_numeric_values.c -lm -o $@
test-scalar: all build/test_scalar_api build/test_numeric_values
	build/test_scalar_api
	build/test_numeric_values
	SPIRV_VAL="$(SPIRV_VAL)" python3 tests/test_scalar_numeric.py
sanitize-scalar: | build/.ready
	$(CC) $(CFLAGS) -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined $(LIB_SOURCES) tests/test_scalar_api.c -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free -lm -o build/test_scalar_api_sanitize
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 build/test_scalar_api_sanitize

.PHONY: test-functions sanitize-functions
build/test_function_api: tests/test_function_api.c $(HEADERS) build/libdoe_compiler.so | build/.ready
	$(CC) $(CFLAGS) tests/test_function_api.c -Lbuild -ldoe_compiler -Wl,-rpath,'$$ORIGIN' -lm -o $@
test-functions: all build/test_function_api
	build/test_function_api
	SPIRV_VAL="$(SPIRV_VAL)" python3 tests/test_functions.py
sanitize-functions: | build/.ready
	$(CC) $(CFLAGS) -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined $(LIB_SOURCES) src/cli.c src/cli_json.c -lm -o build/doe-compiler-c-functions-sanitize
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 DOE_COMPILER_CLI="$(CURDIR)/build/doe-compiler-c-functions-sanitize" SPIRV_VAL="$(SPIRV_VAL)" python3 tests/test_functions.py

# Current owner-focused developer checks. Historical aggregate targets and
# independently sealed acceptance remain separate and unchanged.
.PHONY: test-numeric-builtins
build/test_m12_numeric: src/numeric.c src/numeric_math.c src/numeric_environment.c tests/test_m12_numeric.c $(HEADERS) | build/.ready
	$(CC) $(CFLAGS) src/numeric.c src/numeric_math.c src/numeric_environment.c tests/test_m12_numeric.c -lm -o $@
test-numeric-builtins: all build/test_m12_numeric build/test_m12_api
	build/test_m12_numeric
	build/test_m12_api
	DOE_COMPILER_CLI="$(CURDIR)/build/doe-compiler-c" SPIRV_VAL="$(SPIRV_VAL)" python3 tests/test_m12_builtins.py

build/test_m12_api: $(LIB_SOURCES) tests/test_m12_api.c $(HEADERS) | build/.ready
	$(CC) $(CFLAGS) -pthread $(LIB_SOURCES) tests/test_m12_api.c -lm -o $@

.PHONY: test-module
build/test_m13_module: $(LIB_SOURCES) tests/test_m13_module.c $(HEADERS) | build/.ready
	$(CC) $(CFLAGS) $(LIB_SOURCES) tests/test_m13_module.c -lm -o $@
build/test_m13_cpp: tests/test_m13_cpp.cpp $(HEADERS) build/libdoe_compiler.so | build/.ready
	$(CXX) $(CXXFLAGS) tests/test_m13_cpp.cpp -Lbuild -ldoe_compiler -Wl,-rpath,'$$ORIGIN' -lm -o $@
test-module: all build/test_m13_module build/test_m13_cpp
	build/test_m13_module
	build/test_m13_cpp
	DOE_COMPILER_CLI="$(CURDIR)/build/doe-compiler-c" SPIRV_VAL="$(SPIRV_VAL)" python3 tests/test_m13_modules.py

.PHONY: test-subgroups
build/test_subgroup_api: $(LIB_SOURCES) tests/test_subgroup_api.c $(HEADERS) | build/.ready
	$(CC) $(CFLAGS) $(LIB_SOURCES) tests/test_subgroup_api.c -lm -o $@
test-subgroups: all build/test_subgroup_api
	build/test_subgroup_api
	DOE_COMPILER_CLI="$(CURDIR)/build/doe-compiler-c" SPIRV_VAL="$(SPIRV_VAL)" python3 tests/test_subgroups.py

.PHONY: test-resources
build/test_resource_api: $(LIB_SOURCES) tests/test_resource_api.c $(HEADERS) | build/.ready
	$(CC) $(CFLAGS) $(LIB_SOURCES) tests/test_resource_api.c -lm -o $@
build/test_resources_cpp: tests/test_resources_cpp.cpp include/doe_compiler_resources.h build/libdoe_compiler.so | build/.ready
	$(CXX) $(CXXFLAGS) tests/test_resources_cpp.cpp -Lbuild -ldoe_compiler -Wl,-rpath,'$$ORIGIN' -o $@
test-resources: all build/test_resource_api build/test_resources_cpp
	build/test_resource_api
	build/test_resources_cpp
	DOE_COMPILER_CLI="$(CURDIR)/build/doe-compiler-c" SPIRV_VAL="$(SPIRV_VAL)" python3 tests/test_images.py

.PHONY: test-stages
build/test_stage_api: $(LIB_SOURCES) tests/test_stage_api.c $(HEADERS) | build/.ready
	$(CC) $(CFLAGS) $(LIB_SOURCES) tests/test_stage_api.c -lm -o $@
build/test_stages_cpp: tests/test_stages_cpp.cpp $(HEADERS) build/libdoe_compiler.so | build/.ready
	$(CXX) $(CXXFLAGS) tests/test_stages_cpp.cpp -Lbuild -ldoe_compiler -Wl,-rpath,'$$ORIGIN' -o $@
test-stages: all build/test_stage_api build/test_stages_cpp
	build/test_stage_api
	build/test_stages_cpp
	cd tests && DOE_COMPILER_CLI="$(CURDIR)/build/doe-compiler-c" SPIRV_VAL="$(SPIRV_VAL)" python3 -m unittest test_stages

# ABI8 uses the existing stage front door, with an explicit matrix schema.
build/test_matrix_api: $(LIB_SOURCES) tests/test_matrix_api.c $(HEADERS) | build/.ready
	$(CC) $(CFLAGS) $(LIB_SOURCES) tests/test_matrix_api.c -lm -o $@
build/test_matrix_cpp: tests/test_matrix_cpp.cpp $(HEADERS) build/libdoe_compiler.so | build/.ready
	$(CXX) $(CXXFLAGS) tests/test_matrix_cpp.cpp -Lbuild -ldoe_compiler -Wl,-rpath,'$$ORIGIN' -lm -o $@
test-matrices: build/test_matrix_api build/test_matrix_cpp build/doe-compiler-c
	build/test_matrix_api
	build/test_matrix_cpp
	DOE_COMPILER_CLI="$(CURDIR)/build/doe-compiler-c" SPIRV_VAL="$(SPIRV_VAL)" PYTHONPATH=tests python3 tests/test_matrices.py
.PHONY: test-matrices

build/test_library_module: $(LIB_SOURCES) tests/test_library_module.c $(HEADERS) | build/.ready
	$(CC) $(CFLAGS) $(LIB_SOURCES) tests/test_library_module.c -lm -o $@
test-library-modules: build/test_library_module
	build/test_library_module
.PHONY: test-library-modules

test-discard: build/doe-compiler-c
	DOE_COMPILER_CLI="$(CURDIR)/build/doe-compiler-c" SPIRV_VAL="$(SPIRV_VAL)" python3 tests/test_discard.py
.PHONY: test-discard


build/test_atomic_api: $(LIB_SOURCES) tests/test_atomic_api.c $(HEADERS) | build/.ready
	$(CC) $(CFLAGS) $(LIB_SOURCES) tests/test_atomic_api.c -lm -o $@
test-atomics: build/doe-compiler-c build/test_atomic_api test-atomic-cpp
	build/test_atomic_api
	DOE_COMPILER_CLI="$(CURDIR)/build/doe-compiler-c" SPIRV_VAL="$(SPIRV_VAL)" python3 tests/test_atomics.py
.PHONY: test-atomics

.PHONY: test-pointers
test-pointers: build/doe-compiler-c build/test_pointer_api ../../../bench/kernels/monte_carlo_paths_fixed_samples.wgsl
	build/test_pointer_api
	DOE_COMPILER_CLI="$(abspath build/doe-compiler-c)" SPIRV_VAL="$(SPIRV_VAL)" python3 -m unittest discover -s tests -p test_pointers.py -v
build/test_pointer_api: tests/test_pointer_api.c $(LIB_SOURCES) $(HEADERS) | build/.ready
	$(CC) $(CFLAGS) tests/test_pointer_api.c $(LIB_SOURCES) -lm -o $@

.PHONY: print-library-sources
print-library-sources:
	@echo $(LIB_SOURCES)

.PHONY: test-text
test-text: build/test_text_api build/test_text_failure build/test_text_abi test-phony
	build/test_text_api build/text_control.metal
	build/test_text_failure
	build/test_text_abi
	$(CXX) -std=c++17 -O2 -fsanitize=undefined -Wno-attributes -Itargets/metal/tests/host -Ibuild tests/test_text_host.cpp -o build/test_text_host
	build/test_text_host
build/test_text_api: $(LIB_SOURCES) $(HEADERS) tests/test_text_api.c | build/.ready
	$(CC) $(CFLAGS) $(LIB_SOURCES) tests/test_text_api.c -lm -o $@
build/test_text_failure: $(LIB_SOURCES) $(HEADERS) tests/test_text_failure.c | build/.ready
	$(CC) $(CFLAGS) $(LIB_SOURCES) tests/test_text_failure.c -Wl,--wrap=fesetenv -lm -o $@
build/test_text_abi: build/libdoe_compiler.so include/doe_compiler_text.h tests/test_text_abi.cpp | build/.ready
	$(CXX) $(CXXFLAGS) tests/test_text_abi.cpp -Lbuild -ldoe_compiler -Wl,-rpath,'$$ORIGIN' -o $@

# Public HLSL consumer checks use the exact selected compiler artifact on either
# native shared-library platform; they do not invoke DXC or a GPU.
TEXT_TEST_BUILD ?= build
.PHONY: test-hlsl-text
test-hlsl-text: $(TEXT_TEST_BUILD)/test_hlsl_api $(TEXT_TEST_BUILD)/test_hlsl_abi
	$(TEXT_TEST_BUILD)/test_hlsl_api
	$(TEXT_TEST_BUILD)/test_hlsl_abi
$(TEXT_TEST_BUILD)/test_hlsl_api: tests/test_hlsl_api.c tests/hlsl_fixture.h $(HEADERS) $(SHARED_LIBRARY)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) tests/test_hlsl_api.c $(abspath $(SHARED_LIBRARY)) -Wl,-rpath,$(abspath $(dir $(SHARED_LIBRARY))) -lm -o $@
$(TEXT_TEST_BUILD)/test_hlsl_abi: tests/test_hlsl_abi.cpp tests/hlsl_fixture.h $(HEADERS) $(SHARED_LIBRARY)
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(EXTRA_CFLAGS) tests/test_hlsl_abi.cpp $(abspath $(SHARED_LIBRARY)) -Wl,-rpath,$(abspath $(dir $(SHARED_LIBRARY))) -o $@

build/test_component_math_api: $(LIB_SOURCES) tests/test_component_math_api.c $(HEADERS) | build/.ready
	$(CC) $(CFLAGS) $(LIB_SOURCES) tests/test_component_math_api.c -lm -o $@
.PHONY: test-component-math
test-component-math: all build/test_component_math_api
	build/test_component_math_api
	DOE_COMPILER_CLI="$(CURDIR)/build/doe-compiler-c" SPIRV_VAL="$(SPIRV_VAL)" python3 tests/test_component_math.py
	python3 tests/test_export_symbols.py

.PHONY: test-type-aliases test-workgroup-types
build/test_alias_workgroup_api: $(LIB_SOURCES) $(HEADERS) tests/test_alias_workgroup_api.c | build/.ready
	$(CC) $(CFLAGS) $(LIB_SOURCES) tests/test_alias_workgroup_api.c -lm -o $@
test-type-aliases: build/doe-compiler-c build/test_alias_workgroup_api
	build/test_alias_workgroup_api
	DOE_COMPILER_CLI="$(CURDIR)/build/doe-compiler-c" SPIRV_VAL="$(SPIRV_VAL)" python3 tests/test_type_aliases.py
test-workgroup-types: build/doe-compiler-c
	DOE_COMPILER_CLI="$(CURDIR)/build/doe-compiler-c" SPIRV_VAL="$(SPIRV_VAL)" python3 tests/test_workgroup_types.py
.PHONY: sanitize-type-aliases
sanitize-type-aliases: | build/.ready
	$(CC) $(CFLAGS) -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined $(LIB_SOURCES) tests/test_alias_workgroup_api.c -lm -o build/test_alias_workgroup_api_sanitize
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 build/test_alias_workgroup_api_sanitize

ENVIRONMENT_FAULT_FLAGS = -Wl,--wrap=fegetenv,--wrap=fegetround,--wrap=feholdexcept,--wrap=fesetround,--wrap=feclearexcept,--wrap=fetestexcept,--wrap=fesetenv
build/test_numeric_environment: src/numeric_environment.c tests/test_numeric_environment.c $(HEADERS) | build/.ready
	$(CC) $(CFLAGS) src/numeric_environment.c tests/test_numeric_environment.c $(ENVIRONMENT_FAULT_FLAGS) -lm -o $@
.PHONY: test-numeric-environment
test-numeric-environment: build/test_numeric_environment build/test_numeric_values
	build/test_numeric_environment
	build/test_numeric_values
	python3 tests/test_numeric_host.py

# Ordinary shared-library consumers, independently runnable on native hosts.
CONSUMER_BUILD ?= build
CONSUMER_COMPILER_LINK = $(abspath $(SHARED_LIBRARY)) -Wl,-rpath,$(abspath $(dir $(SHARED_LIBRARY)))
.PHONY: test-atomic-cpp test-phony test-consumers
test-consumers: test-atomic-cpp test-phony
test-atomic-cpp: $(CONSUMER_BUILD)/test_atomic_cpp
	$(CONSUMER_BUILD)/test_atomic_cpp
test-phony: $(CONSUMER_BUILD)/test_phony_api
	$(CONSUMER_BUILD)/test_phony_api
$(CONSUMER_BUILD)/test_atomic_cpp: tests/test_atomic_cpp.cpp $(HEADERS) $(SHARED_LIBRARY)
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(EXTRA_CFLAGS) tests/test_atomic_cpp.cpp $(CONSUMER_COMPILER_LINK) -o $@
$(CONSUMER_BUILD)/test_phony_api: tests/test_phony_api.c $(HEADERS) $(SHARED_LIBRARY)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) tests/test_phony_api.c $(CONSUMER_COMPILER_LINK) -lm -o $@

# Runtime controls are opt-in and require a named software ICD. The backend
# owns compilation of its archive; this owner links only its public boundary.
VULKAN_BUILD ?= $(abspath $(CONSUMER_BUILD)/vulkan)
VULKAN_CONSUMER_DEPS = ../backends/vulkan/Makefile $(wildcard ../backends/vulkan/src/*.c ../backends/vulkan/src/*.h ../backends/vulkan/include/*.h) ../common/include/doe_copy_footprint.h
$(VULKAN_BUILD)/libdoe_vk.a: $(VULKAN_CONSUMER_DEPS)
	$(MAKE) -C ../backends/vulkan all OUT="$(abspath $(VULKAN_BUILD))" CC="$(CC)"
$(CONSUMER_BUILD)/test_atomic_vulkan: tests/test_atomic_vulkan.c $(HEADERS) $(SHARED_LIBRARY) $(VULKAN_BUILD)/libdoe_vk.a $(VULKAN_CONSUMER_DEPS)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -Iinclude -I../backends/vulkan/include tests/test_atomic_vulkan.c $(VULKAN_BUILD)/libdoe_vk.a $(CONSUMER_COMPILER_LINK) -ldl -o $@
$(CONSUMER_BUILD)/test_pointer_vulkan: tests/test_pointer_vulkan.c $(HEADERS) $(SHARED_LIBRARY) $(VULKAN_BUILD)/libdoe_vk.a $(VULKAN_CONSUMER_DEPS)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -Iinclude -I../backends/vulkan/include tests/test_pointer_vulkan.c $(VULKAN_BUILD)/libdoe_vk.a $(CONSUMER_COMPILER_LINK) -ldl -o $@
.PHONY: require-software-vulkan test-atomic-vulkan test-pointer-vulkan test-cpu-vulkan
require-software-vulkan:
	@if [ ! -f "$(SOFTWARE_VULKAN_ICD)" ]; then echo "Set SOFTWARE_VULKAN_ICD to an existing CPU Vulkan ICD JSON file" >&2; exit 1; fi
test-atomic-vulkan: require-software-vulkan $(CONSUMER_BUILD)/test_atomic_vulkan tests/atomic_operations.wgsl ../../../bench/kernels/workgroup_atomic.wgsl
	VK_ICD_FILENAMES="$(abspath $(SOFTWARE_VULKAN_ICD))" $(CONSUMER_BUILD)/test_atomic_vulkan "$(abspath tests/atomic_operations.wgsl)" "$(abspath ../../../bench/kernels/workgroup_atomic.wgsl)" --allow-software
test-pointer-vulkan: require-software-vulkan $(CONSUMER_BUILD)/test_pointer_vulkan tests/pointer_operations.wgsl
	VK_ICD_FILENAMES="$(abspath $(SOFTWARE_VULKAN_ICD))" $(CONSUMER_BUILD)/test_pointer_vulkan "$(abspath tests/pointer_operations.wgsl)" --allow-software
test-cpu-vulkan: test-atomic-vulkan test-pointer-vulkan
