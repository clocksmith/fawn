/* SPDX-License-Identifier: MIT
 * Runtime receipt writer: disabled unless DOE_RUNTIME_RECEIPT names a file;
 * exact line bytes, escaping, truncation, sequence and process identity; one
 * complete line per event under concurrent writers; unwritable paths do not
 * fail the caller. Usage: runtime-receipt EMPTY_SCRATCH_DIRECTORY */
#include "../src/runtime_receipt.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define THREADS 8
#define EVENTS_PER_THREAD 200

static char *slurp(const char *path, size_t *size)
{
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    assert(fseek(file, 0, SEEK_END) == 0);
    const long length = ftell(file);
    assert(length >= 0);
    assert(fseek(file, 0, SEEK_SET) == 0);
    char *bytes = malloc((size_t)length + 1);
    assert(bytes);
    assert(fread(bytes, 1, (size_t)length, file) == (size_t)length);
    bytes[length] = 0;
    assert(fclose(file) == 0);
    *size = (size_t)length;
    return bytes;
}

static void join_path(char *out, size_t capacity, const char *directory, const char *name)
{
    const int written = snprintf(out, capacity, "%s/%s", directory, name);
    assert(written > 0 && (size_t)written < capacity);
}

static void disabled(const char *directory)
{
    char path[4096];
    join_path(path, sizeof(path), directory, "disabled.jsonl");
    assert(unsetenv(NATIVE_RECEIPT_ENVIRONMENT) == 0);
    assert(!native_receipt_enabled());
    native_receipt_lifecycle_event(NATIVE_RECEIPT_INSTANCE_CREATED);
    native_receipt_debug(NATIVE_RECEIPT_COMPUTE_PASS, false, (WGPUStringView){"unset", 5}, true);
    native_receipt_label((WGPUStringView){"unset", 5}, true);
    assert(setenv(NATIVE_RECEIPT_ENVIRONMENT, "", 1) == 0); /* Empty is unset, not the cwd. */
    assert(!native_receipt_enabled());
    native_receipt_lifecycle_event(NATIVE_RECEIPT_DEVICE_CREATED);
    struct stat info;
    assert(stat(path, &info) != 0);
}

static void exact_lines(const char *directory)
{
    char path[4096];
    join_path(path, sizeof(path), directory, "exact.jsonl");
    assert(setenv(NATIVE_RECEIPT_ENVIRONMENT, path, 1) == 0);
    assert(native_receipt_enabled());
    native_receipt_lifecycle_event(NATIVE_RECEIPT_INSTANCE_CREATED);
    WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
    info.vendor = (WGPUStringView){"google", WGPU_STRLEN};
    info.architecture = (WGPUStringView){"swiftshader", 11};
    info.device = (WGPUStringView){NULL, 0};
    info.description = (WGPUStringView){"Swift\"Shader\\", WGPU_STRLEN};
    info.backendType = WGPUBackendType_Vulkan;
    info.adapterType = WGPUAdapterType_DiscreteGPU;
    info.vendorID = 0x1ae0;
    info.deviceID = 0xc0de;
    native_receipt_adapter(&info);
    static const char nonce[] = "nonce-0123\"\\\n\x01\xff\0tail";
    native_receipt_debug(NATIVE_RECEIPT_COMPUTE_PASS, false, (WGPUStringView){nonce, sizeof(nonce) - 1}, true);
    native_receipt_debug(NATIVE_RECEIPT_COMMAND_ENCODER, true, (WGPUStringView){"m", WGPU_STRLEN}, false);
    char long_label[NATIVE_RECEIPT_LABEL_BYTES + 9];
    memset(long_label, 'a', sizeof(long_label));
    native_receipt_label((WGPUStringView){long_label, sizeof(long_label)}, true);
    native_receipt_lifecycle_event(NATIVE_RECEIPT_DEVICE_CREATED);

    const unsigned long pid = (unsigned long)getpid(), ppid = (unsigned long)getppid();
    char expected[8192];
    char truncated[NATIVE_RECEIPT_LABEL_BYTES + 1];
    memset(truncated, 'a', NATIVE_RECEIPT_LABEL_BYTES);
    truncated[NATIVE_RECEIPT_LABEL_BYTES] = 0;
    const int written = snprintf(expected, sizeof(expected),
        "{\"doe_runtime_receipt\":1,\"seq\":1,\"pid\":%lu,\"ppid\":%lu,\"event\":\"instance_created\"}\n"
        "{\"doe_runtime_receipt\":1,\"seq\":2,\"pid\":%lu,\"ppid\":%lu,\"event\":\"adapter_returned\","
        "\"backend_type\":%u,\"adapter_type\":%u,\"vendor_id\":6880,\"device_id\":49374,"
        "\"vendor\":\"google\",\"vendor_bytes\":6,\"architecture\":\"swiftshader\",\"architecture_bytes\":11,"
        "\"device\":\"\",\"device_bytes\":0,\"description\":\"Swift\\\"Shader\\\\\",\"description_bytes\":13}\n"
        "{\"doe_runtime_receipt\":1,\"seq\":3,\"pid\":%lu,\"ppid\":%lu,\"event\":\"compute_pass_push_debug_group\","
        "\"label\":\"nonce-0123\\\"\\\\\\u000a\\u0001\\u00ff\\u0000tail\",\"label_bytes\":20,\"accepted\":true}\n"
        "{\"doe_runtime_receipt\":1,\"seq\":4,\"pid\":%lu,\"ppid\":%lu,\"event\":\"command_encoder_insert_debug_marker\","
        "\"label\":\"m\",\"label_bytes\":1,\"accepted\":false}\n"
        "{\"doe_runtime_receipt\":1,\"seq\":5,\"pid\":%lu,\"ppid\":%lu,\"event\":\"object_label\","
        "\"label\":\"%s\",\"label_bytes\":%u,\"label_truncated\":true,\"accepted\":true}\n"
        "{\"doe_runtime_receipt\":1,\"seq\":6,\"pid\":%lu,\"ppid\":%lu,\"event\":\"device_created\"}\n",
        pid, ppid, pid, ppid, (unsigned)WGPUBackendType_Vulkan, (unsigned)WGPUAdapterType_DiscreteGPU,
        pid, ppid, pid, ppid, pid, ppid, truncated, (unsigned)sizeof(long_label), pid, ppid);
    assert(written > 0 && (size_t)written < sizeof(expected));
    size_t size = 0;
    char *bytes = slurp(path, &size);
    assert(bytes);
    if (size != (size_t)written || memcmp(bytes, expected, size) != 0) {
        fprintf(stderr, "receipt mismatch\nexpected:\n%s\nactual:\n%s\n", expected, bytes);
        abort();
    }
    free(bytes);
    struct stat mode;
    assert(stat(path, &mode) == 0 && (mode.st_mode & 0777) == 0600);
    assert(unlink(path) == 0);
}

static void *writer(void *argument)
{
    const unsigned thread = (unsigned)(size_t)argument;
    char label[32];
    for (unsigned i = 0; i < EVENTS_PER_THREAD; ++i) {
        const int length = snprintf(label, sizeof(label), "t%u-e%u", thread, i);
        assert(length > 0);
        native_receipt_debug(NATIVE_RECEIPT_COMPUTE_PASS, true, (WGPUStringView){label, (size_t)length}, true);
    }
    return NULL;
}

static void concurrent(const char *directory, unsigned long long first_sequence)
{
    char path[4096];
    join_path(path, sizeof(path), directory, "concurrent.jsonl");
    assert(setenv(NATIVE_RECEIPT_ENVIRONMENT, path, 1) == 0);
    pthread_t threads[THREADS];
    for (size_t i = 0; i < THREADS; ++i)
        assert(pthread_create(&threads[i], NULL, writer, (void *)i) == 0);
    for (size_t i = 0; i < THREADS; ++i) assert(pthread_join(threads[i], NULL) == 0);
    size_t size = 0;
    char *bytes = slurp(path, &size);
    assert(bytes && size && bytes[size - 1] == '\n');
    enum { TOTAL = THREADS * EVENTS_PER_THREAD };
    static unsigned char seen_sequence[TOTAL], seen_label[THREADS][EVENTS_PER_THREAD];
    size_t lines = 0;
    for (char *line = bytes, *end; *line; line = end + 1) {
        end = strchr(line, '\n');
        assert(end);
        *end = 0;
        unsigned long long sequence = 0;
        unsigned thread = 0, event = 0;
        int consumed = 0;
        assert(sscanf(line, "{\"doe_runtime_receipt\":1,\"seq\":%llu,", &sequence) == 1);
        const char *label = strstr(line, "\"label\":\"t");
        assert(label && sscanf(label, "\"label\":\"t%u-e%u\"%n", &thread, &event, &consumed) == 2 && consumed > 0);
        assert(sequence >= first_sequence && sequence - first_sequence < TOTAL);
        assert(thread < THREADS && event < EVENTS_PER_THREAD);
        assert(!seen_sequence[sequence - first_sequence]++);
        assert(!seen_label[thread][event]++);
        ++lines;
    }
    assert(lines == TOTAL);
    free(bytes);
    assert(unlink(path) == 0);
}

static void unwritable(const char *directory)
{
    char path[4096];
    join_path(path, sizeof(path), directory, "missing-directory/receipt.jsonl");
    assert(setenv(NATIVE_RECEIPT_ENVIRONMENT, path, 1) == 0);
    native_receipt_lifecycle_event(NATIVE_RECEIPT_INSTANCE_CREATED); /* Reported on stderr only. */
    struct stat info;
    assert(stat(path, &info) != 0);
    assert(unsetenv(NATIVE_RECEIPT_ENVIRONMENT) == 0);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    disabled(argv[1]);
    exact_lines(argv[1]);
    concurrent(argv[1], 7);
    unwritable(argv[1]);
    printf("PASS runtime receipt: disabled when unset or empty, exact escaped/truncated lines with pid, ppid "
        "and sequence, %d complete lines from %d concurrent writers, unwritable path leaves the caller unchanged\n",
        THREADS * EVENTS_PER_THREAD, THREADS);
    return 0;
}
