# GCC-only, default-off owner build. No Zig tool or generated Zig input is used.
.DEFAULT_GOAL := all
.DELETE_ON_ERROR:

ROOT := $(abspath ../../..)
OWNER := $(ROOT)/runtime/c/dropin-policy
BUILD ?= /tmp/doe-dropin-policy
BUILD := $(abspath $(BUILD))
ifeq ($(origin CC),default)
CC := gcc
endif
ifeq ($(origin CXX),default)
CXX := g++
endif
CC := $(shell realpath "$$(command -v "$(CC)")")
CXX := $(shell realpath "$$(command -v "$(CXX)")")
AR := $(shell realpath "$$(command -v "$(AR)")")
PYTHON ?= python3
TARGET ?= x86_64-linux-gnu
PROFILE ?= 1
# Fixed mandatory options cannot be weakened through an inherited CFLAGS.
override CFLAGS := -std=c11 -O2 -fPIC -fvisibility=hidden -pthread -Wall -Wextra -Wpedantic -Werror -Wconversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes
override CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -fno-exceptions -fno-rtti -pthread
VERIFY := $(OWNER)/tools/verify_archive.py
GENERATOR := $(OWNER)/tools/generate_config.py
INPUTS := $(ROOT)/config/dropin-abi-behavior.json $(ROOT)/config/dropin-symbol-ownership.json
SCHEMAS := $(ROOT)/config/dropin-abi-behavior.schema.json $(ROOT)/config/dropin-symbol-ownership.schema.json
HEADERS := $(wildcard $(OWNER)/include/*.h) $(wildcard $(OWNER)/src/*.h)
SOURCES := $(OWNER)/src/policy.c
SETTINGS := $(BUILD)/build_settings.json
GENERATED := $(BUILD)/generated_config.c
OBJECTS := $(BUILD)/policy.o $(BUILD)/generated_config.o
ARCHIVE := $(BUILD)/libdoe_dropin_policy.a
RECEIPT := $(ARCHIVE).receipt.json
VERIFY_ARGS = --repo-root "$(ROOT)" --target "$(TARGET)" --profile "$(PROFILE)" --compiler "$(CC)" --archiver "$(AR)"

.PHONY: all verify check check-generation FORCE
all: verify

$(BUILD):
	mkdir -p "$@"

# Hashes, rather than only mtimes, invalidate changed source, options and tools.
# This runs each invocation but preserves its mtime when content is identical.
$(SETTINGS): FORCE $(INPUTS) $(SCHEMAS) $(HEADERS) $(SOURCES) $(GENERATOR) $(VERIFY) $(OWNER)/Makefile | $(BUILD)
	$(PYTHON) "$(VERIFY)" $(VERIFY_ARGS) --write-build-settings "$@"

$(GENERATED): FORCE $(INPUTS) $(SCHEMAS) $(GENERATOR) $(SETTINGS) $(HEADERS) | $(BUILD)
	$(PYTHON) "$(GENERATOR)" --behavior "$(word 1,$(INPUTS))" --ownership "$(word 2,$(INPUTS))" --profile "$(PROFILE)" --output "$@"

$(BUILD)/policy.o: $(SOURCES) $(HEADERS) $(SETTINGS) | $(BUILD)
	"$(CC)" $(CFLAGS) -I "$(OWNER)/include" -c "$(OWNER)/src/policy.c" -o "$@"
	$(PYTHON) "$(VERIFY)" $(VERIFY_ARGS) --record-object --settings "$(SETTINGS)" --source "$(OWNER)/src/policy.c" --object "$@"

$(BUILD)/generated_config.o: $(GENERATED) $(HEADERS) $(SETTINGS) | $(BUILD)
	"$(CC)" $(CFLAGS) -I "$(OWNER)/include" -c "$(GENERATED)" -o "$@"
	$(PYTHON) "$(VERIFY)" $(VERIFY_ARGS) --record-object --settings "$(SETTINGS)" --source "$(GENERATED)" --object "$@"

$(ARCHIVE): $(OBJECTS)
	rm -f "$@.tmp"
	"$(AR)" rcsD "$@.tmp" $(OBJECTS)
	mv "$@.tmp" "$@"

$(RECEIPT): $(ARCHIVE) $(SETTINGS) $(VERIFY)
	$(PYTHON) "$(VERIFY)" $(VERIFY_ARGS) --record --archive "$(ARCHIVE)" --settings "$(SETTINGS)"

verify: $(RECEIPT)
	$(PYTHON) "$(VERIFY)" $(VERIFY_ARGS) --archive "$(ARCHIVE)"

$(BUILD)/test_api: $(OWNER)/tests/test_api.c $(ARCHIVE) $(HEADERS) $(SETTINGS)
	"$(CC)" $(CFLAGS) -I "$(OWNER)/include" "$<" "$(ARCHIVE)" -Wl,--wrap=getenv -o "$@"

$(BUILD)/test_faults: $(OWNER)/tests/test_faults.c $(ARCHIVE) $(HEADERS) $(SETTINGS)
	"$(CC)" $(CFLAGS) -I "$(OWNER)/include" "$<" "$(ARCHIVE)" -Wl,--wrap=pthread_mutex_lock -Wl,--wrap=pthread_mutex_unlock -o "$@"

$(BUILD)/test_cpp: $(OWNER)/tests/test_cpp.cpp $(ARCHIVE) $(HEADERS) $(SETTINGS)
	"$(CXX)" $(CXXFLAGS) -I "$(OWNER)/include" "$<" "$(ARCHIVE)" -o "$@"

$(BUILD)/abi_probe: $(OWNER)/integration/abi_probe.c $(ARCHIVE) $(HEADERS) $(SETTINGS)
	"$(CC)" $(CFLAGS) -I "$(OWNER)/include" -I "$(ROOT)/runtime/zig/vendor/webgpu-headers" "$<" "$(ARCHIVE)" -o "$@"

$(BUILD)/native_consumer: $(OWNER)/integration/native_consumer.c $(SETTINGS)
	"$(CC)" $(CFLAGS) -I "$(ROOT)/runtime/zig/vendor/webgpu-headers" "$<" -ldl -o "$@"

check-generation:
	PYTHONDONTWRITEBYTECODE=1 $(PYTHON) "$(OWNER)/tests/test_generation.py"

check: verify $(BUILD)/test_api $(BUILD)/test_faults $(BUILD)/test_cpp $(BUILD)/abi_probe $(BUILD)/native_consumer check-generation
	ulimit -c 0; "$(BUILD)/test_api"
	ulimit -c 0; "$(BUILD)/test_faults"
	"$(BUILD)/test_cpp"
	"$(BUILD)/abi_probe"
