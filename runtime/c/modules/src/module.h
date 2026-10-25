/* SPDX-License-Identifier: MIT
 * Retained module contracts: deterministic policy models and host numerics.
 * Parsed JSON stays owned by the caller; result streams are caller-owned. */
#ifndef DOE_C_MODULE_H
#define DOE_C_MODULE_H
#include "json.h"
#include "host_io.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef enum module_kind {
    MODULE_COMPUTE, MODULE_EFFECTS, MODULE_PATH, MODULE_SCHEDULER, MODULE_SDF, MODULE_NUMERIC
} module_kind;
typedef enum module_type {
    MODULE_STRING, MODULE_U8, MODULE_U32, MODULE_U64, MODULE_F64, MODULE_BOOL,
    MODULE_OBJECT, MODULE_ARRAY, MODULE_ANY, MODULE_OPTIONAL
} module_type;
typedef struct module_shape module_shape;
typedef struct module_field {
    const char *name;
    const module_shape *shape;
    const char *default_json; /* NULL means required; defaults belong to the source contract. */
} module_field;
struct module_shape {
    module_type type;
    const module_field *fields;
    size_t count;
    const module_shape *element;
    size_t array_length; /* zero is an unconstrained slice */
};
typedef struct module_contract {
    const char *id;
    module_kind kind;
    const module_shape *request;
    const module_shape *policy;
} module_contract;
extern const module_contract module_contracts[6];
extern const module_shape module_numeric_registry;
extern const module_shape module_proof_links;

/* Validate and serialize in declared field order, filling retained defaults.
 * Failure leaves the caller's temporary output unpublished. */
bool module_normalize(FILE *out, const doe_json_value *value, const module_shape *shape);
bool module_json(FILE *out, const doe_json_value *value);
bool module_string(FILE *out, const char *text, size_t length);
bool module_key(FILE *out, const char *name);
void module_hash(const char *bytes, size_t length, char digest[65]);
const doe_json_value *module_get(const doe_json_value *value, const char *key);
uint64_t module_uint(const doe_json_value *value, const char *key);
bool module_equal(const doe_json_value *value, const char *text);
bool module_contains(const doe_json_value *array, const doe_json_value *needle);
bool module_sum(const doe_json_value *array, const char *key, uint64_t *result);
bool module_add(uint64_t a, uint64_t b, uint64_t *out);
bool module_mul(uint64_t a, uint64_t b, uint64_t *out);
bool module_model(FILE *out, const module_contract *contract, const doe_json_value *request,
    const doe_json_value *policy, const char request_hash[65]);
bool module_numeric(FILE *out, const doe_json_value *request, const char *policy_text,
    size_t policy_size, const char *policy_path, char *error, size_t error_capacity);
bool module_write_file(const char *path, const char *bytes, size_t length);
#endif
