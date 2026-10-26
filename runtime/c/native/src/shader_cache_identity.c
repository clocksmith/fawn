/* SPDX-License-Identifier: MIT */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE
#endif
#include "shader_cache_identity.h"
#include "doe_compiler.h"
#include "../../shader/include/doe_shader_artifact.h"
#include "../../common/include/doe_sha256.h"

#include <limits.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__APPLE__)
#include <dlfcn.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#elif defined(__linux__)
#include <link.h>
#endif

typedef DoeCompilerStatus (*compiler_entry)(uint32_t, const char *, size_t,
    uint32_t *, size_t, DoeCompilerResult *, size_t);
#if defined(_WIN64)
/* The canonical x64 Windows native builder links the separate audited compiler
 * import library. A plain &function can name a local thunk without dllimport;
 * the IAT slot instead contains the exact loaded implementation address. */
extern compiler_entry __imp_doe_compiler_compile;
#endif

enum { IMAGE_LIMIT = 512 * 1024 * 1024 };
typedef struct image_hash {
    uint8_t digest[32];
    size_t bytes;
    size_t spans;
} image_hash;

static void little64(uint8_t out[8], uint64_t value)
{
    for (size_t i = 0; i < 8; ++i) out[i] = (uint8_t)(value >> (i * 8));
}

/* A domain-separated sequence of position/length/content hashes; the common
 * SHA-256 implementation streams each span in constant space. No second hash
 * implementation or image-sized allocation lives in the native adapter. */
static bool span_bytes(image_hash *hash, uintptr_t base, uintptr_t address,
    const void *bytes, size_t size)
{
    uint8_t record[88] = {0};
    if (!size) return true;
    if (address < base || size > IMAGE_LIMIT - hash->bytes) return false;
    memcpy(record, "DOE-IMAGE-SPAN-v1", 17);
    memcpy(record + 24, hash->digest, 32);
    little64(record + 56, (uint64_t)(address - base));
    little64(record + 64, (uint64_t)size);
    /* The content digest joins a separately delimited metadata digest. */
    uint8_t combined[64];
    doe_common_sha256(record, sizeof(record), combined);
    doe_common_sha256(bytes, size, combined + 32);
    doe_common_sha256(combined, sizeof(combined), hash->digest);
    hash->bytes += size;
    hash->spans++;
    return true;
}

static bool span(image_hash *hash, uintptr_t base, uintptr_t address, size_t size)
{
    return span_bytes(hash, base, address, (const void *)address, size);
}

#if defined(__linux__)
/* A linker build ID binds relocation-bearing tables/configuration without
 * hashing loader addresses. Require it: code/rodata alone is not the whole
 * artifact identity. These notes are read from the loaded image, never disk. */
static bool elf_build_id(const struct dl_phdr_info *info, image_hash *hash)
{
    const uintptr_t base = (uintptr_t)info->dlpi_addr;
    for (size_t i = 0; i < info->dlpi_phnum; ++i) {
        const ElfW(Phdr) *p = &info->dlpi_phdr[i];
        if (p->p_type != PT_NOTE) continue;
        if (p->p_vaddr > UINTPTR_MAX - base || p->p_memsz > IMAGE_LIMIT) return false;
        uintptr_t address = base + p->p_vaddr;
        size_t offset = 0;
        while (offset < p->p_memsz) {
            if (p->p_memsz - offset < sizeof(ElfW(Nhdr))) return false;
            ElfW(Nhdr) note;
            memcpy(&note, (const void *)(address + offset), sizeof(note));
            offset += sizeof(note);
            size_t name = (size_t)note.n_namesz;
            size_t data = (size_t)note.n_descsz;
            if (name > SIZE_MAX - 3 || data > SIZE_MAX - 3) return false;
            name = (name + 3) & ~(size_t)3;
            data = (data + 3) & ~(size_t)3;
            if (name > p->p_memsz - offset || data > p->p_memsz - offset - name) return false;
            if (note.n_type == NT_GNU_BUILD_ID && note.n_namesz == 4 &&
                memcmp((const void *)(address + offset), "GNU", 4) == 0 &&
                note.n_descsz >= 16 && note.n_descsz <= 64)
                return span(hash, base, address + offset + name, note.n_descsz);
            offset += name + data;
        }
    }
    return false;
}

typedef struct elf_search {
    uintptr_t anchor;
    image_hash hash;
    bool found;
    bool valid;
} elf_search;

static int elf_image(struct dl_phdr_info *info, size_t size, void *opaque)
{
    elf_search *search = opaque;
    (void)size;
    const uintptr_t base = (uintptr_t)info->dlpi_addr;
    for (size_t i = 0; i < info->dlpi_phnum; ++i) {
        const ElfW(Phdr) *p = &info->dlpi_phdr[i];
        if (p->p_type != PT_LOAD || p->p_vaddr > UINTPTR_MAX - base) continue;
        uintptr_t start = base + p->p_vaddr;
        if (search->anchor >= start && search->anchor - start < p->p_memsz)
            search->found = true;
    }
    if (!search->found) return 0;
    if (!elf_build_id(info, &search->hash)) return 1;
    search->valid = true;
    for (size_t i = 0; i < info->dlpi_phnum; ++i) {
        const ElfW(Phdr) *p = &info->dlpi_phdr[i];
        bool readonly = p->p_type == PT_LOAD && (p->p_flags & PF_R) && !(p->p_flags & PF_W);
        if (!readonly) continue;
        if (p->p_vaddr > UINTPTR_MAX - base || p->p_memsz > SIZE_MAX) {
            search->valid = false;
            return 1;
        }
        uintptr_t start = base + p->p_vaddr;
        size_t length = (size_t)p->p_memsz;
        if (length > UINTPTR_MAX - start) {
            search->valid = false;
            return 1;
        }
        if (!span(&search->hash, base, start, length)) {
            search->valid = false;
            return 1;
        }
    }
    return 1;
}

static bool loaded_image(uintptr_t anchor, image_hash *hash)
{
    elf_search search = {.anchor = anchor};
    (void)dl_iterate_phdr(elf_image, &search);
    if (!search.found || !search.valid || !search.hash.spans) return false;
    *hash = search.hash;
    return true;
}
#elif defined(_WIN32)
static bool loaded_image(uintptr_t anchor, image_hash *hash)
{
    HMODULE module = NULL;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            (LPCWSTR)anchor, &module)) return false;
    const uint8_t *image = (const uint8_t *)module;
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)image;
    bool valid = false;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 ||
        dos->e_lfanew > 1024 * 1024) goto done;
    const IMAGE_NT_HEADERS *nt = (const IMAGE_NT_HEADERS *)(image + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || !nt->OptionalHeader.SizeOfImage ||
        nt->OptionalHeader.SizeOfImage > IMAGE_LIMIT) goto done;
    const uintptr_t base = (uintptr_t)module;
    const uintptr_t end = base + nt->OptionalHeader.SizeOfImage;
    if (end < base) goto done;
    for (uintptr_t address = base; address < end;) {
        MEMORY_BASIC_INFORMATION region;
        if (VirtualQuery((const void *)address, &region, sizeof(region)) != sizeof(region) ||
            region.AllocationBase != module || region.RegionSize == 0 ||
            region.RegionSize > UINTPTR_MAX - (uintptr_t)region.BaseAddress) goto done;
        uintptr_t next = (uintptr_t)region.BaseAddress + region.RegionSize;
        if (next > end) next = end;
        if (next <= address) goto done;
        DWORD access = region.Protect & 0xffu;
        bool readonly = access == PAGE_READONLY || access == PAGE_EXECUTE_READ;
        if (region.State == MEM_COMMIT && readonly && !(region.Protect & PAGE_GUARD) &&
            !span(hash, base, address, (size_t)(next - address))) goto done;
        address = next;
    }
    valid = hash->spans != 0;
done:
    (void)FreeLibrary(module);
    return valid;
}
#elif defined(__APPLE__)
static bool loaded_image(uintptr_t anchor, image_hash *hash)
{
    Dl_info owner;
    if (!dladdr((const void *)anchor, &owner) || !owner.dli_fbase) return false;
    const struct mach_header_64 *header = owner.dli_fbase;
    if (header->magic != MH_MAGIC_64 || header->sizeofcmds > 16u * 1024u * 1024u)
        return false;
    intptr_t slide = 0;
    bool found = false;
    for (uint32_t i = 0; i < _dyld_image_count(); ++i) {
        if (_dyld_get_image_header(i) == (const struct mach_header *)header) {
            slide = _dyld_get_image_vmaddr_slide(i);
            found = true;
            break;
        }
    }
    if (!found) return false;
    const uint8_t *commands = (const uint8_t *)(header + 1);
    size_t offset = 0;
    bool uuid_found = false;
    for (uint32_t i = 0; i < header->ncmds; ++i) {
        if (offset > header->sizeofcmds || header->sizeofcmds - offset < sizeof(struct load_command))
            return false;
        const struct load_command *command = (const struct load_command *)(commands + offset);
        if (command->cmdsize < sizeof(*command) || command->cmdsize > header->sizeofcmds - offset)
            return false;
        if (command->cmd == LC_UUID) {
            if (uuid_found || command->cmdsize < sizeof(struct uuid_command)) return false;
            const struct uuid_command *uuid = (const struct uuid_command *)command;
            const uint8_t empty[16] = {0};
            if (!memcmp(uuid->uuid, empty, sizeof(empty)) ||
                !span(hash, (uintptr_t)header, (uintptr_t)uuid, sizeof(*uuid))) return false;
            uuid_found = true;
        }
        if (command->cmd == LC_SEGMENT_64) {
            if (command->cmdsize < sizeof(struct segment_command_64)) return false;
            const struct segment_command_64 *segment = (const struct segment_command_64 *)command;
            /* dyld makes relocated __DATA_CONST read-only after writing ASLR
             * pointers. Bind those original bytes by LC_UUID, like the ELF
             * build ID, and hash only originally immutable mapped payload. */
            if ((segment->initprot & VM_PROT_READ) && !(segment->initprot & VM_PROT_WRITE)) {
                uintptr_t address = (uintptr_t)segment->vmaddr + (uintptr_t)slide;
                if (segment->vmsize > UINTPTR_MAX - address) return false;
                uintptr_t end = address + (uintptr_t)segment->vmsize;
                while (address < end) {
                    mach_vm_address_t region_address = address;
                    mach_vm_size_t region_size = 0;
                    vm_region_basic_info_data_64_t info;
                    mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
                    mach_port_t object = MACH_PORT_NULL;
                    kern_return_t result = mach_vm_region(mach_task_self(), &region_address,
                        &region_size, VM_REGION_BASIC_INFO_64, (vm_region_info_t)&info, &count, &object);
                    if (object != MACH_PORT_NULL) (void)mach_port_deallocate(mach_task_self(), object);
                    if (result != KERN_SUCCESS || region_address > address ||
                        region_size > UINTPTR_MAX - region_address) return false;
                    uintptr_t next = (uintptr_t)(region_address + region_size);
                    if (next > end) next = end;
                    if (next <= address) return false;
                    if ((info.protection & VM_PROT_READ) && !(info.protection & VM_PROT_WRITE) &&
                        !span(hash, (uintptr_t)header, address, (size_t)(next - address))) return false;
                    address = next;
                }
            }
        }
        offset += command->cmdsize;
    }
    return uuid_found && hash->spans != 0;
}
#else
static bool loaded_image(uintptr_t anchor, image_hash *hash)
{
    (void)anchor;
    (void)hash;
    return false;
}
#endif

bool native_shader_cache_identity(uint8_t digest[32])
{
    if (!digest) return false;
    uintptr_t native_address = 0;
    uintptr_t compiler_address = 0;
    uintptr_t shader_address = 0;
    bool (*native_function)(uint8_t *) = native_shader_cache_identity;
#if defined(_WIN64)
    compiler_entry compiler_function = __imp_doe_compiler_compile;
#elif defined(_WIN32)
    compiler_entry compiler_function = NULL; /* No unverified x86 thunk guess. */
#else
    compiler_entry compiler_function = doe_compiler_compile;
#endif
    doe_shader_status (*shader_function)(const doe_shader *,
        const DoeCompilerEntrySelectionV5 *, const DoeCompilerOverrideValueV13 *, size_t,
        const DoeCompilerMetalPipelineOptionsV15 *, const void *, size_t, doe_shader **) =
        doe_shader_artifact_read;
    _Static_assert(sizeof(shader_function) == sizeof(shader_address), "shader function address size");
    _Static_assert(sizeof(native_function) == sizeof(native_address), "native function address size");
    _Static_assert(sizeof(compiler_function) == sizeof(compiler_address), "compiler function address size");
    memcpy(&native_address, &native_function, sizeof(native_address));
    if (!compiler_function) return false;
    memcpy(&compiler_address, &compiler_function, sizeof(compiler_address));
    memcpy(&shader_address, &shader_function, sizeof(shader_address));
    image_hash native = {0};
    image_hash compiler = {0};
    image_hash shader = {0};
    if (!loaded_image(native_address, &native) || !loaded_image(compiler_address, &compiler) ||
        !loaded_image(shader_address, &shader))
        return false;
    uint8_t combined[120] = {0};
    memcpy(combined, "DOE-LOADED-OWNERS-v2", 20);
    memcpy(combined + 24, native.digest, 32);
    memcpy(combined + 56, compiler.digest, 32);
    memcpy(combined + 88, shader.digest, 32);
    doe_common_sha256(combined, sizeof(combined), digest);
    return true;
}
