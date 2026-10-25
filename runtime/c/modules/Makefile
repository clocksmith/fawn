# SPDX-License-Identifier: MIT
CC = gcc
PYTHON ?= python3
include ../build-root.mk
BUILD ?= $(DOE_C_TMP)/modules
CPPFLAGS += -D_POSIX_C_SOURCE=200809L -Isrc -I../runtime-cli/src
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Werror -fno-fast-math -ffp-contract=off
CLI = ../runtime-cli/src
# The policy, routing, numeric JSON and SHA owners are shared host C code.
include sources.mk
SOURCES = $(MODULE_SOURCES) $(BUILD)/quirk_toggles.c
HEADERS = src/module.h $(wildcard $(CLI)/*.h)
.PHONY: all check check-package bench-bin
all: $(BUILD)/doe-c-module-runner
$(BUILD):
	mkdir -p $@
$(BUILD)/quirk_toggles.c: ../runtime-cli/tools/gen_toggle_registry.py ../../../config/quirk-toggle-registry.json | $(BUILD)
	$(PYTHON) $^ $@
$(BUILD)/doe-c-module-runner: $(SOURCES) $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SOURCES) -lm -o $@
bench-bin: all
	mkdir -p bin
	cp '$(BUILD)/doe-c-module-runner' bin/doe-c-module-runner
check: all
	$(PYTHON) tests/test_modules.py --binary '$(abspath $(BUILD))/doe-c-module-runner' --work '$(abspath $(BUILD))/tests'
RUNTIME_BINARY ?= $(DOE_C_TMP)/runtime-cli/doe-c-runtime
check-package: check
	env DOE_BIN='$(abspath $(RUNTIME_BINARY))' DOE_MODULE_RUNNER='$(abspath $(BUILD))/doe-c-module-runner' \
	  node ../../../packages/doe-gpu/test/integration/test-integration-c-modules.js
