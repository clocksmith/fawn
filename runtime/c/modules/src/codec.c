/* SPDX-License-Identifier: MIT */
#include "module.h"
#include "numeric_stability.h"
#include "sha256.h"
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

bool module_string(FILE *out, const char *text, size_t length)
{
    if (fputc('"', out) == EOF) return false;
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)text[i];
        int result;
        switch (c) {
        case '"': result = fputs("\\\"", out); break;
        case '\\': result = fputs("\\\\", out); break;
        case '\b': result = fputs("\\b", out); break;
        case '\f': result = fputs("\\f", out); break;
        case '\n': result = fputs("\\n", out); break;
        case '\r': result = fputs("\\r", out); break;
        case '\t': result = fputs("\\t", out); break;
        default: result = c < 32 ? fprintf(out, "\\u%04x", c) : fputc(c, out); break;
        }
        if (result < 0) return false;
    }
    return fputc('"', out) != EOF;
}

bool module_key(FILE *out, const char *name)
{
    return module_string(out, name, strlen(name)) && fputc(':', out) != EOF;
}

bool module_json(FILE *out, const doe_json_value *v)
{
    if (!v || v->kind == DOE_JSON_NULL) return fputs("null", out) >= 0;
    switch (v->kind) {
    case DOE_JSON_NULL: return fputs("null", out) >= 0;
    case DOE_JSON_BOOL: return fputs(v->boolean ? "true" : "false", out) >= 0;
    case DOE_JSON_STRING: return module_string(out, v->string, v->string_size);
    case DOE_JSON_NUMBER:
        if (v->integer) return fprintf(out, "%s%" PRIu64, v->negative ? "-" : "", v->magnitude) >= 0;
        return doe_ns_write_f64(out, v->number);
    case DOE_JSON_ARRAY:
    case DOE_JSON_OBJECT: {
        bool object = v->kind == DOE_JSON_OBJECT;
        if (fputc(object ? '{' : '[', out) == EOF) return false;
        for (size_t i = 0; i < v->count; i++) {
            if (i && fputc(',', out) == EOF) return false;
            if (object && (!module_string(out, v->members[i].key, v->members[i].key_size) || fputc(':', out) == EOF))
                return false;
            if (!module_json(out, object ? v->members[i].value : v->items[i])) return false;
        }
        return fputc(object ? '}' : ']', out) != EOF;
    }
    }
    return false;
}

/* std.json's typed numeric fields also admit numeric strings. Retain decimal
 * exponent spelling and interior separators, but never whitespace or hex. */
static char *numeric_text(const doe_json_value *v)
{
    if (v->kind != DOE_JSON_STRING || !v->string_size || v->string[0] == '_' ||
        v->string[v->string_size - 1] == '_') return NULL;
    char *text = malloc(v->string_size + 1);
    if (!text) return NULL;
    size_t length = 0;
    for (size_t i = 0; i < v->string_size; i++) {
        char c = v->string[i];
        if (!strchr("0123456789+-.eE_", c) || !c) { free(text); return NULL; }
        if (c != '_') text[length++] = c;
    }
    text[length] = 0;
    return text;
}

bool module_normalize(FILE *out, const doe_json_value *v, const module_shape *s)
{
    if (!v) return false;
    uint64_t number;
    switch (s->type) {
    case MODULE_STRING: return v->kind == DOE_JSON_STRING && module_json(out, v);
    case MODULE_BOOL: return v->kind == DOE_JSON_BOOL && module_json(out, v);
    case MODULE_U8:
    case MODULE_U32:
    case MODULE_U64: {
        uint64_t maximum = s->type == MODULE_U8 ? UINT8_MAX : s->type == MODULE_U32 ? UINT32_MAX : UINT64_MAX;
        if (doe_json_as_u64(v, maximum, &number)) return fprintf(out, "%" PRIu64, number) >= 0;
        if (v->kind == DOE_JSON_NUMBER && v->integer && v->magnitude == 0) return fputc('0', out) != EOF;
        if (v->kind == DOE_JSON_NUMBER && !v->integer && isfinite(v->number) && v->number >= 0 &&
            v->number < 0x1p64 && floor(v->number) == v->number && (uint64_t)v->number <= maximum)
            return fprintf(out, "%" PRIu64, (uint64_t)v->number) >= 0;
        char *text = numeric_text(v);
        if (text) {
            char *end;
            errno = 0;
            unsigned long long integer = strtoull(text, &end, 10);
            bool exact = text[0] != '-' && !errno && end != text && !*end && integer <= maximum;
            double parsed = exact ? 0 : strtod(text, &end);
            bool integral = !exact && end != text && !*end && isfinite(parsed) && parsed >= 0 && parsed < 0x1p64 &&
                floor(parsed) == parsed && (uint64_t)parsed <= maximum;
            bool ok = (exact || integral) && fprintf(out, "%" PRIu64, exact ? (uint64_t)integer : (uint64_t)parsed) >= 0;
            free(text);
            return ok;
        }
        return false;
    }
    case MODULE_F64: {
        if (v->kind == DOE_JSON_NUMBER) return isfinite(v->number) && doe_ns_write_f64(out, v->number);
        char *text = numeric_text(v);
        if (text) {
            char *end;
            double parsed = strtod(text, &end);
            bool ok = end != text && !*end && isfinite(parsed) && doe_ns_write_f64(out, parsed);
            free(text);
            return ok;
        }
        return false;
    }
    case MODULE_ANY: return module_json(out, v);
    case MODULE_OPTIONAL:
        return v->kind == DOE_JSON_NULL ? module_json(out, v) : module_normalize(out, v, s->element);
    case MODULE_ARRAY:
        if (v->kind != DOE_JSON_ARRAY || (s->array_length && v->count != s->array_length) || fputc('[', out) == EOF)
            return false;
        for (size_t i = 0; i < v->count; i++)
            if ((i && fputc(',', out) == EOF) || !module_normalize(out, v->items[i], s->element)) return false;
        return fputc(']', out) != EOF;
    case MODULE_OBJECT:
        if (v->kind != DOE_JSON_OBJECT) return false;
        for (size_t i = 0; i < v->count; i++) {
            bool found = false;
            for (size_t j = 0; j < s->count; j++) {
                const char *key = s->fields[j].name;
                if (strlen(key) == v->members[i].key_size && !memcmp(key, v->members[i].key, strlen(key))) found = true;
            }
            if (!found) return false;
        }
        if (fputc('{', out) == EOF) return false;
        for (size_t i = 0; i < s->count; i++) {
            const module_field *field = &s->fields[i];
            const doe_json_value *child = module_get(v, field->name);
            if ((i && fputc(',', out) == EOF) || !module_key(out, field->name)) return false;
            if (child) {
                if (!module_normalize(out, child, field->shape)) return false;
            } else if (!field->default_json || fputs(field->default_json, out) < 0) return false;
        }
        return fputc('}', out) != EOF;
    }
    return false;
}

void module_hash(const char *bytes, size_t length, char digest[65])
{
    uint8_t raw[32];
    doe_sha256(bytes, length, raw);
    doe_sha256_hex(raw, digest);
}

const doe_json_value *module_get(const doe_json_value *value, const char *key)
{
    return doe_json_member_value(value, key);
}

uint64_t module_uint(const doe_json_value *value, const char *key)
{
    return module_get(value, key)->magnitude;
}

bool module_equal(const doe_json_value *value, const char *text)
{
    return value && value->kind == DOE_JSON_STRING && value->string_size == strlen(text) &&
        !memcmp(value->string, text, value->string_size);
}

bool module_contains(const doe_json_value *array, const doe_json_value *needle)
{
    for (size_t i = 0; i < array->count; i++) {
        const doe_json_value *v = array->items[i];
        if (needle->kind == DOE_JSON_STRING && v->string_size == needle->string_size &&
            !memcmp(v->string, needle->string, v->string_size)) return true;
        if (needle->kind == DOE_JSON_NUMBER && v->magnitude == needle->magnitude) return true;
    }
    return false;
}

bool module_add(uint64_t a, uint64_t b, uint64_t *out)
{
    if (a > UINT64_MAX - b) return false;
    *out = a + b;
    return true;
}

bool module_mul(uint64_t a, uint64_t b, uint64_t *out)
{
    if (b && a > UINT64_MAX / b) return false;
    *out = a * b;
    return true;
}

bool module_sum(const doe_json_value *array, const char *key, uint64_t *result)
{
    *result = 0;
    for (size_t i = 0; i < array->count; i++)
        if (!module_add(*result, module_uint(array->items[i], key), result)) return false;
    return true;
}
