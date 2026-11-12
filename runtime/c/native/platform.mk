# Native artifact/link selection only. Backend admission is runtime-owned.
SHARED_PLATFORM ?= $(shell uname -s)
ifeq ($(SHARED_PLATFORM),Darwin)
SHARED_SUFFIX = dylib
NATIVE_SHARED_FLAGS = -dynamiclib -Wl,-undefined,error \
 '-Wl,-install_name,@rpath/$(notdir $(NATIVE_LIBRARY))' \
 -Wl,-exported_symbols_list,$(BUILD)/native.exports \
 -Wl,-oso_prefix,$(abspath $(BUILD))/
NATIVE_EXPORTS = $(BUILD)/native.exports
PLATFORM_LIBS =
else ifeq ($(SHARED_PLATFORM),Linux)
SHARED_SUFFIX = so
# Spellings GNU ld and LLVM lld (zig cc) both accept. -Bsymbolic equals
# -Bsymbolic-functions because the library exports only functions; the package
# tool refuses a defined dynamic data symbol. The SONAME is the file name, so
# a consumer records that name rather than the build path it linked against.
NATIVE_SHARED_FLAGS = -shared -Wl,-z,defs -Wl,-Bsymbolic \
 -Wl,-soname,$(notdir $(NATIVE_LIBRARY))
NATIVE_EXPORTS =
PLATFORM_LIBS = -ldl
else
$(error Unsupported native build platform $(SHARED_PLATFORM))
endif
# -lNAME, not -l:FILE (lld has no -l: form); the compiler library is lib*.$(SHARED_SUFFIX).
COMPILER_NAME_LINK = -l$(patsubst lib%,%,$(basename $(notdir $(COMPILER_LIBRARY))))
# Shared artifact identity is consumed directly by native, CLI and staging.
# A parse-time recursive make query can capture GNU Make's directory banners.
NATIVE_LIBRARY_BASENAME = libwebgpu_doe_c.$(SHARED_SUFFIX)
