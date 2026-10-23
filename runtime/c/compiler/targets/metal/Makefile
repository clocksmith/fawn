# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Doe contributors
CC = gcc
OUT ?= /tmp/doe-metal-target
CFLAGS = -std=c11 -O2 -fno-fast-math -ffp-contract=off -frounding-math -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wswitch-enum -Werror
SOURCES = text.c types.c numeric.c access.c control.c stage.c
CORE = $(filter-out ../../src/cli.c ../../src/cli_json.c,$(wildcard ../../src/*.c))
.PHONY: all check style clean
all: $(OUT)/test_metal
$(OUT)/test_metal: $(SOURCES) metal.h private.h tests/test_metal.c $(CORE) $(wildcard ../../src/*.h)
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) $(SOURCES) $(CORE) tests/test_metal.c -lm -o $@
check: all style
	$(OUT)/test_metal
style:
	python3 ../../../tools/check_source_size.py $(abspath $(SOURCES) metal.h private.h tests/test_metal.c)
clean:
	rm -f $(OUT)/test_metal
.PHONY: host-control
host-control: all
	$(OUT)/test_metal $(OUT)/control.metal $(OUT)/aggregate.metal
	g++ -std=c++17 -O2 -fno-fast-math -ffp-contract=off -Wno-attributes -Itests/host -I$(OUT) tests/host/check_scalar.cpp -o $(OUT)/check_scalar
	$(OUT)/check_scalar

	g++ -std=c++17 -O2 -fno-fast-math -ffp-contract=off -Wno-attributes -Itests/host -I$(OUT) tests/host/check_layout.cpp -o $(OUT)/check_layout
	$(OUT)/check_layout
.PHONY: library
library: $(OUT)/libdoe_metal_target.a
$(OUT)/%.o: %.c metal.h private.h $(wildcard ../../src/*.h)
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/libdoe_metal_target.a: $(addprefix $(OUT)/,$(SOURCES:.c=.o))
	ar rcs $@ $^
