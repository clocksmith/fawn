/* SPDX-License-Identifier: MIT */
#include "doe_labels.h"
#include <cassert>
#include <cstdlib>
#include <cstring>

static void *allocate(void *, const void *, size_t size, size_t) {
    return std::malloc(size);
}
static void release(void *, const void *, void *memory, size_t, size_t) {
    std::free(memory);
}
int main() {
    doe_label_config config{DOE_LABELS_ABI_VERSION,
                           {nullptr, nullptr, allocate, release}};
    doe_label_store *store = nullptr;
    assert(doe_label_store_create(&config, &store) == DOE_LABEL_OK);
    const unsigned char bytes[]{0, 255, 127};
    assert(doe_label_store_set(store, &config, bytes, sizeof(bytes)) == DOE_LABEL_OK);
    doe_label_view view{};
    assert(doe_label_store_get(store, &config, &view) == DOE_LABEL_OK);
    assert(view.length == sizeof(bytes));
    assert(std::memcmp(view.data, bytes, sizeof(bytes)) == 0);
    assert(doe_label_store_destroy(store) == DOE_LABEL_OK);
}
