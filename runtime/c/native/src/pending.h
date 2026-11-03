/* SPDX-License-Identifier: MIT */
#ifndef DOE_NATIVE_PENDING_H
#define DOE_NATIVE_PENDING_H
#include "internal.h"

struct doe_vk_surface;
struct native_surface;
struct native_async;

/* Closed variants on the device's existing pending owner, not separate queues.
 * The device mutex protects the list and every record's transitions. */
typedef enum native_pending_kind {
    NATIVE_PENDING_COMMANDS,
    NATIVE_PENDING_SURFACE,
    NATIVE_PENDING_ASYNC
} native_pending_kind;
struct native_pending {
    native_pending *next;
    native_device *device;
    native_command_handle native;
    doe_resource_commands **resources;
    size_t count;
    native_pending_kind kind;
    uint64_t serial;
    struct native_async *async; /* Only NATIVE_PENDING_ASYNC; owns this embedded record. */
    bool replay; /* Command variant: settle the resource owner's open replay. */
    struct doe_vk_surface *frame; /* Present commands borrow their backend surface lease. */
    struct native_surface *surface;
    bool armed, ready;
};
#endif
