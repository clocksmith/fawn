CC = gcc
BUILD ?= /tmp/doe-c-common
CPPFLAGS += -Iinclude
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Werror
.PHONY: check
check: $(BUILD)/test_copy_footprint
	$(BUILD)/test_copy_footprint
$(BUILD)/test_copy_footprint: tests/test_copy_footprint.c include/doe_copy_footprint.h
	mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@
