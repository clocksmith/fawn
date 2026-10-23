/* SPDX-License-Identifier: MIT
 * Private retained-consumer seam. Never exported from a native DSO.
 * Frozen private boundary; no implementation or sealed independent cases yet.
 */
#ifndef DOE_LABELS_NATIVE_PRIVATE_H
#define DOE_LABELS_NATIVE_PRIVATE_H

#include "doe_labels.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Same map algorithms, one artifact-local static owner. No caller can obtain its
 * store pointer, replace its allocator or destroy it through this boundary.
 * Quiescent native unload owns shutdown. Hidden ELF binding is mandatory. */
doe_label_status doe_labels_native_set(const void *handle,
                                       const unsigned char *data, size_t length);
doe_label_status doe_labels_native_get(const void *handle, doe_label_view *out);
doe_label_status doe_labels_native_remove(const void *handle);

#ifdef __cplusplus
}
#endif
#endif
