# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Doe contributors
CC = gcc
OUT ?= /tmp/doe-hlsl-target
CFLAGS = -std=c11 -O2 -fno-fast-math -ffp-contract=off -frounding-math -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wswitch-enum -Werror
SOURCES = text.c types.c resources.c image_types.c image.c numeric.c access.c control.c stage.c graphics.c
TESTS = tests/test_hlsl.c tests/test_images.c tests/test_graphics.c
CORE = $(filter-out ../../src/cli.c ../../src/cli_json.c,$(wildcard ../../src/*.c))
.PHONY: all check style clean
all: $(OUT)/test_hlsl
$(OUT)/test_hlsl: $(SOURCES) hlsl.h private.h $(TESTS) tests/test_support.h $(CORE) $(wildcard ../../src/*.h ../../include/*.h)
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) $(SOURCES) $(CORE) $(TESTS) -lm -o $@
check: all style
	$(OUT)/test_hlsl
style:
	python3 ../../../tools/check_source_size.py $(abspath $(SOURCES) hlsl.h private.h $(TESTS) tests/test_support.h)
clean:
	rm -f $(OUT)/test_hlsl
.PHONY: library
library: $(OUT)/libdoe_hlsl_target.a
$(OUT)/%.o: %.c hlsl.h private.h $(wildcard ../../src/*.h ../../include/*.h)
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/libdoe_hlsl_target.a: $(addprefix $(OUT)/,$(SOURCES:.c=.o))
	ar rcs $@ $^
.PHONY: host-control
host-control: all
	python3 tests/host_control.py $(OUT)
