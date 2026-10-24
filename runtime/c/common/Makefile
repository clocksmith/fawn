CC = gcc
include $(dir $(lastword $(MAKEFILE_LIST)))../build-root.mk
BUILD ?= $(DOE_C_TMP)/common
CPPFLAGS += -Iinclude
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Werror
.PHONY: check
check: $(BUILD)/test_copy_footprint $(BUILD)/test_dispatch_guard
	$(BUILD)/test_copy_footprint
	$(BUILD)/test_dispatch_guard
$(BUILD)/test_copy_footprint: tests/test_copy_footprint.c include/doe_copy_footprint.h
	mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@
$(BUILD)/test_dispatch_guard: tests/test_dispatch_guard.c include/doe_dispatch_guard.h
	mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@
