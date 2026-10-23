/*
 * doe_napi_device_events.c — uncaptured-error and device-lost delivery for
 * providers without the Doe setters.
 *
 * webgpu.h takes both callbacks once, in WGPUDeviceDescriptor. The addon
 * registers JS listeners later, so requestDevice passes a per-device slot as
 * the callbacks' userdata and the Doe setter entry points become shims that
 * swap the slot's binding. Callers keep one code path for both providers.
 */
#include "doe_napi_internal.h"
#include <stdatomic.h>

typedef struct DeviceEventSlot {
    void* device; /* Set once the device exists; cleared under the list lock. */
    _Atomic(DeviceCallbackBinding*) uncaptured;
    _Atomic(DeviceCallbackBinding*) lost;
    _Atomic uint32_t in_flight; /* Trampolines currently reading a binding. */
    struct DeviceEventSlot* next;
} DeviceEventSlot;

static DeviceEventSlot* g_slots = NULL;
static atomic_flag g_slots_lock = ATOMIC_FLAG_INIT;
bool g_device_events_standard = false;

static void slots_lock(void) { while (atomic_flag_test_and_set(&g_slots_lock)) {} }
static void slots_unlock(void) { atomic_flag_clear(&g_slots_lock); }

static DeviceEventSlot* slot_find(void* device) {
    for (DeviceEventSlot* slot = g_slots; slot; slot = slot->next)
        if (slot->device == device) return slot;
    return NULL;
}

/* After the exchange, no trampoline can newly observe the old binding; wait
 * out any that loaded it before, so the caller may release it. */
static void slot_store(_Atomic(DeviceCallbackBinding*)* field, DeviceEventSlot* slot,
    DeviceCallbackBinding* binding) {
    atomic_exchange(field, binding);
    while (atomic_load(&slot->in_flight)) {}
}

static void standard_uncaptured(const void* device, WGPUErrorType type, WGPUStringView message,
    void* userdata1, void* userdata2) {
    (void)device; (void)userdata2;
    DeviceEventSlot* slot = (DeviceEventSlot*)userdata1;
    atomic_fetch_add(&slot->in_flight, 1);
    DeviceCallbackBinding* binding = atomic_load(&slot->uncaptured);
    if (binding) uncaptured_error_native_callback(type, message, binding, NULL);
    atomic_fetch_sub(&slot->in_flight, 1);
}

static void standard_lost(const void* device, WGPUDeviceLostReason reason, WGPUStringView message,
    void* userdata1, void* userdata2) {
    (void)device; (void)userdata2;
    DeviceEventSlot* slot = (DeviceEventSlot*)userdata1;
    atomic_fetch_add(&slot->in_flight, 1);
    DeviceCallbackBinding* binding = atomic_load(&slot->lost);
    if (binding) lost_native_callback(reason, message.data, message.data ? message.length : 0, binding);
    atomic_fetch_sub(&slot->in_flight, 1);
    /* Destroyed and FailedCreation are final: the provider reports nothing
     * more for this device, so the slot leaves the list and is freed. Other
     * losses keep it, because errors may still be delivered afterwards. */
    if (reason != 0x00000002u /* Destroyed */ && reason != 0x00000004u /* FailedCreation */) return;
    slots_lock();
    for (DeviceEventSlot** link = &g_slots; *link; link = &(*link)->next) {
        if (*link == slot) { *link = slot->next; break; }
    }
    slots_unlock();
    while (atomic_load(&slot->in_flight)) {}
    free(slot);
}

void* device_events_describe(WGPUDeviceDescriptor* descriptor) {
    if (!g_device_events_standard) return NULL;
    DeviceEventSlot* slot = (DeviceEventSlot*)calloc(1, sizeof(*slot));
    if (!slot) return NULL;
    descriptor->uncapturedErrorCallbackInfo = (WGPUUncapturedErrorCallbackInfo){
        NULL, standard_uncaptured, slot, NULL};
    descriptor->deviceLostCallbackInfo = (WGPUDeviceLostCallbackInfo){
        NULL, WGPU_CALLBACK_MODE_ALLOW_SPONTANEOUS, standard_lost, slot, NULL};
    slots_lock();
    slot->next = g_slots;
    g_slots = slot;
    slots_unlock();
    return slot;
}

/* A failed request still delivers FailedCreation, which frees the slot. */
void device_events_attach(void* opaque, void* device) {
    if (!opaque || !device) return;
    slots_lock();
    ((DeviceEventSlot*)opaque)->device = device;
    slots_unlock();
}

/* The store runs under the list lock, so a final lost callback cannot free the
 * slot meanwhile; trampolines never take that lock while counted in flight. */
static void shim_set_uncaptured(void* device, void (*callback)(uint32_t, WGPUStringView, void*, void*),
    void* userdata1, void* userdata2) {
    (void)userdata2;
    slots_lock();
    DeviceEventSlot* slot = slot_find(device);
    if (slot) slot_store(&slot->uncaptured, slot, callback ? (DeviceCallbackBinding*)userdata1 : NULL);
    slots_unlock();
}

static void shim_register_lost(void* device, void (*callback)(uint32_t, const char*, size_t, void*),
    void* userdata) {
    slots_lock();
    DeviceEventSlot* slot = slot_find(device);
    if (slot) slot_store(&slot->lost, slot, callback ? (DeviceCallbackBinding*)userdata : NULL);
    slots_unlock();
}

void device_events_install(void) {
    g_device_events_standard = !pfn_doeNativeDeviceSetUncapturedErrorCallback &&
        !pfn_doeNativeDeviceRegisterLostCallback;
    if (!g_device_events_standard) return;
    pfn_doeNativeDeviceSetUncapturedErrorCallback = shim_set_uncaptured;
    pfn_doeNativeDeviceRegisterLostCallback = shim_register_lost;
}
