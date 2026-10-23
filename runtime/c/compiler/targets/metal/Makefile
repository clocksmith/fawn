# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Doe contributors
CC = gcc
OUT ?= /tmp/doe-metal-target
CFLAGS = -std=c11 -O2 -fno-fast-math -ffp-contract=off -frounding-math -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wswitch-enum -Werror
SOURCES = text.c zero.c types.c numeric.c access.c control.c stage.c handles.c image.c graphics.c
CORE = $(filter-out ../../src/cli.c ../../src/cli_json.c,$(wildcard ../../src/*.c))
.PHONY: all check style clean
all: $(OUT)/test_metal $(OUT)/test_images
$(OUT)/test_metal: $(SOURCES) metal.h private.h ../ir_view.h ../closure.h ../scratch.h ../stage_view.h ../tests/array_length_source.h ../tests/closure_source.h tests/fixture.h tests/test_metal.c $(CORE) $(wildcard ../../src/*.h)
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) $(SOURCES) $(CORE) tests/test_metal.c -lm -o $@
$(OUT)/test_images: $(SOURCES) metal.h private.h ../ir_view.h ../closure.h ../scratch.h ../stage_view.h ../tests/array_length_source.h ../tests/closure_source.h tests/fixture.h tests/test_images.c $(CORE) $(wildcard ../../src/*.h)
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) $(SOURCES) $(CORE) tests/test_images.c -lm -o $@
check: all style $(OUT)/test_ir_view $(OUT)/test_closure $(OUT)/test_graphics $(OUT)/test_matrices
	$(OUT)/test_metal
	$(OUT)/test_images
	$(OUT)/test_ir_view
	$(OUT)/test_closure
	$(OUT)/test_graphics
	$(OUT)/test_matrices
style:
	python3 ../../../tools/check_source_size.py $(abspath $(SOURCES) metal.h private.h ../ir_view.h ../closure.h ../scratch.h ../stage_view.h ../tests/array_length_source.h ../tests/closure_source.h tests/fixture.h tests/test_metal.c tests/test_images.c tests/host/image_control.h tests/host/vector_control.h)
clean:
	rm -f $(OUT)/test_metal $(OUT)/test_images
.PHONY: host-control
host-control: all
	$(OUT)/test_metal $(OUT)/control.metal $(OUT)/aggregate.metal $(OUT)/array_length.metal
	g++ -std=c++17 -O2 -fno-fast-math -ffp-contract=off -Wno-attributes -Itests/host -I$(OUT) tests/host/check_scalar.cpp -o $(OUT)/check_scalar
	$(OUT)/check_scalar

	g++ -std=c++17 -O2 -fno-fast-math -ffp-contract=off -Wno-attributes -Itests/host -I$(OUT) tests/host/check_layout.cpp -o $(OUT)/check_layout
	$(OUT)/check_layout
	$(CXX) -std=c++17 -O2 -fsanitize=undefined -Wno-attributes -Itests/host -I$(OUT) tests/host/check_array_length.cpp -o $(OUT)/check_array_length
	$(OUT)/check_array_length
.PHONY: image-host-control
image-host-control: all
	$(OUT)/test_images $(OUT)/images.metal
	g++ -std=c++17 -O2 -fno-fast-math -ffp-contract=off -Wno-attributes -Itests/host -I$(OUT) tests/host/check_images.cpp -o $(OUT)/check_images
	$(OUT)/check_images
.PHONY: library
library: $(OUT)/libdoe_metal_target.a
$(OUT)/%.o: %.c metal.h private.h ../ir_view.h ../closure.h ../scratch.h ../stage_view.h ../tests/array_length_source.h ../tests/closure_source.h $(wildcard ../../src/*.h)
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/libdoe_metal_target.a: $(addprefix $(OUT)/,$(SOURCES:.c=.o))
	ar rcs $@ $^

$(OUT)/test_ir_view: ../tests/test_ir_view.c ../ir_view.h ../closure.h ../scratch.h ../stage_view.h $(wildcard ../../src/*.h)
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) ../tests/test_ir_view.c -o $@

$(OUT)/test_closure: ../tests/test_closure.c ../closure.h ../scratch.h ../stage_view.h $(wildcard ../../src/*.h)
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) ../tests/test_closure.c -o $@

$(OUT)/test_graphics: $(SOURCES) metal.h private.h ../ir_view.h ../closure.h ../scratch.h ../stage_view.h ../tests/stage_source.h tests/fixture.h tests/test_graphics.c $(CORE) $(wildcard ../../src/*.h)
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) $(SOURCES) $(CORE) tests/test_graphics.c -lm -o $@
.PHONY: graphics-host-control
graphics-host-control: $(OUT)/test_graphics
	$(OUT)/test_graphics $(OUT)/graphics.metal
	$(CXX) -std=c++17 -O2 -fsanitize=undefined -Wno-attributes -Itests/host -I$(OUT) tests/host/check_graphics.cpp -o $(OUT)/check_graphics
	$(OUT)/check_graphics

$(OUT)/test_matrices: $(SOURCES) metal.h private.h ../closure.h ../scratch.h tests/fixture.h tests/test_matrices.c ../tests/matrix_source.h $(CORE) $(wildcard ../../src/*.h)
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) $(SOURCES) $(CORE) tests/test_matrices.c -lm -o $@
.PHONY: matrix-host-control
matrix-host-control: $(OUT)/test_matrices
	$(OUT)/test_matrices $(OUT)/matrices.metal
	$(CXX) -std=c++17 -O2 -fsanitize=undefined -Wno-attributes -Itests/host -I$(OUT) tests/host/check_matrices.cpp -o $(OUT)/check_matrices
	$(OUT)/check_matrices
