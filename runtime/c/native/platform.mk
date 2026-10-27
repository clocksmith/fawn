# Native artifact/link selection only. Backend admission is runtime-owned.
SHARED_PLATFORM ?= $(shell uname -s)
ifeq ($(SHARED_PLATFORM),Darwin)
SHARED_SUFFIX = dylib
NATIVE_SHARED_FLAGS = -dynamiclib -Wl,-undefined,error \
 '-Wl,-install_name,@rpath/$(notdir $(NATIVE_LIBRARY))' \
 -Wl,-exported_symbols_list,$(BUILD)/native.exports
NATIVE_EXPORTS = $(BUILD)/native.exports
PLATFORM_LIBS =
COMPILER_NAME_LINK = -l$(patsubst lib%,%,$(basename $(notdir $(COMPILER_LIBRARY))))
else ifeq ($(SHARED_PLATFORM),Linux)
SHARED_SUFFIX = so
NATIVE_SHARED_FLAGS = -shared -Wl,-z,defs -Wl,-Bsymbolic-functions
NATIVE_EXPORTS =
PLATFORM_LIBS = -ldl
COMPILER_NAME_LINK = -l:$(notdir $(COMPILER_LIBRARY))
else
$(error Unsupported native build platform $(SHARED_PLATFORM))
endif
# Shared artifact identity is consumed directly by native, CLI and staging.
# A parse-time recursive make query can capture GNU Make's directory banners.
NATIVE_LIBRARY_BASENAME = libwebgpu_doe_c.$(SHARED_SUFFIX)
