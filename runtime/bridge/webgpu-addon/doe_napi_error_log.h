/*
 * doe_napi_error_log.h — the per-device log of provider errors. The provider
 * reports every error to the device's uncaptured-error callback (the package
 * pushes no native error scopes); the callback appends here from any thread,
 * and JS takes the log in report order to route errors to its own scopes.
 */
#ifndef DOE_NAPI_ERROR_LOG_H
#define DOE_NAPI_ERROR_LOG_H
#include <stdatomic.h>
#include <stdint.h>

typedef struct ProviderErrorEntry {
    uint32_t type; /* WGPUErrorType */
    char* message;
    struct ProviderErrorEntry* next;
} ProviderErrorEntry;

typedef struct {
    atomic_flag lock;
    ProviderErrorEntry* head;
    ProviderErrorEntry* tail;
} ProviderErrorLog;

void provider_error_log_append(ProviderErrorLog* log, ProviderErrorEntry* entry);
/* Detaches and returns every entry, oldest first; the caller frees them. */
ProviderErrorEntry* provider_error_log_take(ProviderErrorLog* log);
void provider_error_log_free(ProviderErrorEntry* entry);
/* Moves the global sequence JS reads through providerErrorSequence(). */
void doe_provider_error_sequence_advance(void);

#endif
