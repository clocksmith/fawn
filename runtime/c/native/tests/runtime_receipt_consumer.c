/* SPDX-License-Identifier: MIT
 * Public-ABI receipt control: with DOE_RUNTIME_RECEIPT set, the provider
 * records instance, adapter (with the info GetInfo reports), device, object
 * labels and debug-group/marker labels it actually receives, from this
 * process; after the variable is unset nothing more is written. */
#include "consumer.h"
#include "../src/runtime_receipt.h"
#include <unistd.h>

static char *slurp(const char *path)
{
    FILE *file = fopen(path, "rb");
    assert(file);
    assert(fseek(file, 0, SEEK_END) == 0);
    const long length = ftell(file);
    assert(length > 0);
    assert(fseek(file, 0, SEEK_SET) == 0);
    char *bytes = malloc((size_t)length + 1);
    assert(bytes);
    assert(fread(bytes, 1, (size_t)length, file) == (size_t)length);
    bytes[length] = 0;
    assert(fclose(file) == 0);
    return bytes;
}

/* The receipt line for this event that carries text, from this process. */
static const char *find_line(const char *receipt, const char *event, const char *text)
{
    char needle[160], self[64];
    assert(snprintf(needle, sizeof(needle), "\"event\":\"%s\"", event) < (int)sizeof(needle));
    assert(snprintf(self, sizeof(self), ",\"pid\":%lu,", (unsigned long)getpid()) < (int)sizeof(self));
    for (const char *line = receipt; *line;) {
        const char *end = strchr(line, '\n');
        assert(end);
        const size_t size = (size_t)(end - line);
        char copy[8192];
        assert(size < sizeof(copy));
        memcpy(copy, line, size);
        copy[size] = 0;
        if (strstr(copy, needle) && strstr(copy, self) && (!text || strstr(copy, text))) return line;
        line = end + 1;
    }
    return NULL;
}

int main(void)
{
    const char *root = getenv("TMPDIR");
    char directory[4096];
    assert(snprintf(directory, sizeof(directory), "%s/doe-receipt-XXXXXX", root && *root ? root : "/tmp") <
        (int)sizeof(directory));
    assert(mkdtemp(directory));
    char path[4200];
    assert(snprintf(path, sizeof(path), "%s/receipt.jsonl", directory) < (int)sizeof(path));
    assert(setenv(NATIVE_RECEIPT_ENVIRONMENT, path, 1) == 0);

    state s = {0};
    consumer_init(&s);
    char nonce[64];
    assert(snprintf(nonce, sizeof(nonce), "receipt-nonce-%lu", (unsigned long)getpid()) < (int)sizeof(nonce));
    WGPUBufferDescriptor buffer_desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    buffer_desc.label = (WGPUStringView){nonce, WGPU_STRLEN};
    buffer_desc.size = 16;
    buffer_desc.usage = WGPUBufferUsage_Storage;
    WGPUBuffer storage = wgpuDeviceCreateBuffer(s.device, &buffer_desc);
    assert(storage);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s.device, NULL);
    wgpuCommandEncoderPushDebugGroup(encoder, (WGPUStringView){nonce, WGPU_STRLEN});
    WGPUComputePassDescriptor pass_desc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
    pass_desc.label = (WGPUStringView){"pass-label", WGPU_STRLEN};
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &pass_desc);
    assert(pass);
    wgpuComputePassEncoderPushDebugGroup(pass, (WGPUStringView){nonce, WGPU_STRLEN});
    wgpuComputePassEncoderInsertDebugMarker(pass, (WGPUStringView){"marker", WGPU_STRLEN});
    wgpuComputePassEncoderPopDebugGroup(pass);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    wgpuCommandEncoderPopDebugGroup(encoder);
    submit(&s, encoder);
    work_done(&s);

    char *receipt = slurp(path);
    WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
    assert(wgpuAdapterGetInfo(s.adapter, &info) == WGPUStatus_Success);
    char adapter_fields[128];
    assert(snprintf(adapter_fields, sizeof(adapter_fields), "\"backend_type\":%u,\"adapter_type\":%u,\"vendor_id\":%u,",
        (unsigned)info.backendType, (unsigned)info.adapterType, (unsigned)info.vendorID) < (int)sizeof(adapter_fields));
    wgpuAdapterInfoFreeMembers(info);
    char quoted[96];
    assert(snprintf(quoted, sizeof(quoted), "\"label\":\"%s\"", nonce) < (int)sizeof(quoted));
    assert(find_line(receipt, "instance_created", NULL));
    assert(find_line(receipt, "adapter_returned", adapter_fields));
    assert(find_line(receipt, "device_created", NULL));
    assert(find_line(receipt, "object_label", quoted));
    assert(find_line(receipt, "object_label", "\"label\":\"pass-label\""));
    assert(find_line(receipt, "command_encoder_push_debug_group", quoted));
    assert(find_line(receipt, "compute_pass_push_debug_group", quoted));
    assert(find_line(receipt, "compute_pass_insert_debug_marker", "\"label\":\"marker\",\"label_bytes\":6,\"accepted\":true"));
    const size_t before = strlen(receipt);
    free(receipt);

    assert(unsetenv(NATIVE_RECEIPT_ENVIRONMENT) == 0);
    encoder = wgpuDeviceCreateCommandEncoder(s.device, NULL);
    wgpuCommandEncoderInsertDebugMarker(encoder, (WGPUStringView){"after-unset", WGPU_STRLEN});
    submit(&s, encoder);
    work_done(&s);
    receipt = slurp(path);
    assert(strlen(receipt) == before);
    free(receipt);

    wgpuBufferRelease(storage);
    consumer_finish(&s);
    assert(unlink(path) == 0);
    assert(rmdir(directory) == 0);
    printf("PASS public runtime receipt: instance, adapter info, device, labels and debug groups from this process; "
        "nothing after unset %s\n", adapter_class_scope());
    return 0;
}
