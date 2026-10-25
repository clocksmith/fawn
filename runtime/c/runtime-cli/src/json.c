/* SPDX-License-Identifier: MIT */
#include "json.h"
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct parser {
    const char *text;
    size_t size, at;
    doe_json_document *document;
    doe_json_status status;
} parser;

static bool fail(parser *p, doe_json_status status)
{
    if (p->status == DOE_JSON_OK) p->status = status;
    return false;
}

/* Every allocation is registered before use, so one free releases them all. */
static void *own(parser *p, size_t bytes)
{
    doe_json_document *d = p->document;
    if (d->allocation_count == d->allocation_capacity) {
        size_t capacity = d->allocation_capacity ? d->allocation_capacity * 2 : 64;
        if (capacity > SIZE_MAX / sizeof(void *)) return NULL;
        void **grown = realloc(d->allocations, capacity * sizeof(void *));
        if (!grown) return NULL;
        d->allocations = grown;
        d->allocation_capacity = capacity;
    }
    void *memory = calloc(1, bytes ? bytes : 1);
    if (!memory) return NULL;
    d->allocations[d->allocation_count++] = memory;
    return memory;
}

static void skip_space(parser *p)
{
    while (p->at < p->size) {
        char c = p->text[p->at];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') return;
        p->at++;
    }
}

static bool literal(parser *p, const char *word)
{
    size_t length = strlen(word);
    if (p->size - p->at < length || memcmp(p->text + p->at, word, length) != 0) return fail(p, DOE_JSON_SYNTAX);
    p->at += length;
    return true;
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool read_hex4(parser *p, uint32_t *out)
{
    if (p->size - p->at < 4) return fail(p, DOE_JSON_SYNTAX);
    uint32_t value = 0;
    for (int i = 0; i < 4; i++) {
        int digit = hex_digit(p->text[p->at + (size_t)i]);
        if (digit < 0) return fail(p, DOE_JSON_SYNTAX);
        value = value * 16u + (uint32_t)digit;
    }
    p->at += 4;
    *out = value;
    return true;
}

static size_t put_utf8(char *out, uint32_t code)
{
    if (code < 0x80) { out[0] = (char)code; return 1; }
    if (code < 0x800) {
        out[0] = (char)(0xC0 | (code >> 6));
        out[1] = (char)(0x80 | (code & 0x3F));
        return 2;
    }
    if (code < 0x10000) {
        out[0] = (char)(0xE0 | (code >> 12));
        out[1] = (char)(0x80 | ((code >> 6) & 0x3F));
        out[2] = (char)(0x80 | (code & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (code >> 18));
    out[1] = (char)(0x80 | ((code >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((code >> 6) & 0x3F));
    out[3] = (char)(0x80 | (code & 0x3F));
    return 4;
}

/* Decoded output never exceeds the encoded byte span, so one buffer of that
 * size holds it (a \u escape of 6 bytes yields at most 3, a pair of 12 at most 4). */
static bool read_string(parser *p, const char **out, size_t *out_size)
{
    p->at++; /* opening quote */
    size_t start = p->at, end = start;
    while (end < p->size && p->text[end] != '"') {
        if ((unsigned char)p->text[end] < 0x20) return fail(p, DOE_JSON_SYNTAX);
        end += p->text[end] == '\\' ? 2 : 1;
    }
    if (end >= p->size) return fail(p, DOE_JSON_SYNTAX);
    char *decoded = own(p, end - start + 1);
    if (!decoded) return fail(p, DOE_JSON_OUT_OF_MEMORY);
    size_t length = 0;
    while (p->at < end) {
        char c = p->text[p->at++];
        if (c != '\\') { decoded[length++] = c; continue; }
        char e = p->text[p->at++];
        switch (e) {
        case '"': decoded[length++] = '"'; break;
        case '\\': decoded[length++] = '\\'; break;
        case '/': decoded[length++] = '/'; break;
        case 'b': decoded[length++] = '\b'; break;
        case 'f': decoded[length++] = '\f'; break;
        case 'n': decoded[length++] = '\n'; break;
        case 'r': decoded[length++] = '\r'; break;
        case 't': decoded[length++] = '\t'; break;
        case 'u': {
            uint32_t code;
            if (!read_hex4(p, &code)) return false;
            if (code >= 0xD800 && code <= 0xDBFF) {
                uint32_t low;
                if (p->size - p->at < 2 || p->text[p->at] != '\\' || p->text[p->at + 1] != 'u') return fail(p, DOE_JSON_SYNTAX);
                p->at += 2;
                if (!read_hex4(p, &low) || low < 0xDC00 || low > 0xDFFF) return fail(p, DOE_JSON_SYNTAX);
                code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
            } else if (code >= 0xDC00 && code <= 0xDFFF) {
                return fail(p, DOE_JSON_SYNTAX);
            }
            length += put_utf8(decoded + length, code);
            break;
        }
        default: return fail(p, DOE_JSON_SYNTAX);
        }
    }
    p->at = end + 1; /* closing quote */
    decoded[length] = '\0';
    *out = decoded;
    *out_size = length;
    return true;
}

static bool read_number(parser *p, doe_json_value *value)
{
    size_t start = p->at;
    bool negative = false, integer = true;
    if (p->text[p->at] == '-') { negative = true; p->at++; }
    if (p->at >= p->size) return fail(p, DOE_JSON_SYNTAX);
    if (p->text[p->at] == '0') {
        p->at++;
    } else if (p->text[p->at] >= '1' && p->text[p->at] <= '9') {
        while (p->at < p->size && p->text[p->at] >= '0' && p->text[p->at] <= '9') p->at++;
    } else {
        return fail(p, DOE_JSON_SYNTAX);
    }
    size_t digits_end = p->at;
    if (p->at < p->size && p->text[p->at] == '.') {
        integer = false;
        p->at++;
        size_t fraction = p->at;
        while (p->at < p->size && p->text[p->at] >= '0' && p->text[p->at] <= '9') p->at++;
        if (p->at == fraction) return fail(p, DOE_JSON_SYNTAX);
    }
    if (p->at < p->size && (p->text[p->at] == 'e' || p->text[p->at] == 'E')) {
        integer = false;
        p->at++;
        if (p->at < p->size && (p->text[p->at] == '+' || p->text[p->at] == '-')) p->at++;
        size_t exponent = p->at;
        while (p->at < p->size && p->text[p->at] >= '0' && p->text[p->at] <= '9') p->at++;
        if (p->at == exponent) return fail(p, DOE_JSON_SYNTAX);
    }
    size_t length = p->at - start;
    char *copy = own(p, length + 1);
    if (!copy) return fail(p, DOE_JSON_OUT_OF_MEMORY);
    memcpy(copy, p->text + start, length);
    copy[length] = '\0';
    errno = 0;
    value->number = strtod(copy, NULL);
    value->kind = DOE_JSON_NUMBER;
    value->negative = negative;
    if (integer) {
        uint64_t magnitude = 0;
        bool fits = true;
        for (size_t i = start + (negative ? 1u : 0u); i < digits_end; i++) {
            uint64_t digit = (uint64_t)(p->text[i] - '0');
            if (magnitude > (UINT64_MAX - digit) / 10u) { fits = false; break; }
            magnitude = magnitude * 10u + digit;
        }
        value->integer = fits;
        value->magnitude = fits ? magnitude : 0;
    }
    if (!isfinite(value->number) && integer == false) return fail(p, DOE_JSON_SYNTAX);
    return true;
}

typedef struct growth {
    void *items;
    size_t count, capacity, item_size;
} growth;

static bool grow_push(growth *g, const void *item)
{
    if (g->count == g->capacity) {
        size_t capacity = g->capacity ? g->capacity * 2 : 8;
        if (capacity > SIZE_MAX / g->item_size) return false;
        void *grown = realloc(g->items, capacity * g->item_size);
        if (!grown) return false;
        g->items = grown;
        g->capacity = capacity;
    }
    memcpy((char *)g->items + g->count * g->item_size, item, g->item_size);
    g->count++;
    return true;
}

/* Moves the temporary growth buffer into document-owned storage. */
static void *settle(parser *p, growth *g)
{
    void *owned = own(p, g->count * g->item_size);
    if (owned && g->count) memcpy(owned, g->items, g->count * g->item_size);
    free(g->items);
    g->items = NULL;
    return owned;
}

static doe_json_value *read_value(parser *p, unsigned depth);

static bool read_array(parser *p, doe_json_value *value, unsigned depth)
{
    p->at++;
    growth items = {.item_size = sizeof(doe_json_value *)};
    skip_space(p);
    if (p->at < p->size && p->text[p->at] == ']') {
        p->at++;
    } else {
        for (;;) {
            doe_json_value *item = read_value(p, depth + 1);
            if (!item || !grow_push(&items, &item)) {
                free(items.items);
                return item ? fail(p, DOE_JSON_OUT_OF_MEMORY) : false;
            }
            skip_space(p);
            if (p->at < p->size && p->text[p->at] == ',') { p->at++; continue; }
            if (p->at < p->size && p->text[p->at] == ']') { p->at++; break; }
            free(items.items);
            return fail(p, DOE_JSON_SYNTAX);
        }
    }
    value->kind = DOE_JSON_ARRAY;
    value->count = items.count;
    value->items = settle(p, &items);
    return value->items ? true : fail(p, DOE_JSON_OUT_OF_MEMORY);
}

static bool read_object(parser *p, doe_json_value *value, unsigned depth)
{
    p->at++;
    growth members = {.item_size = sizeof(doe_json_member)};
    skip_space(p);
    if (p->at < p->size && p->text[p->at] == '}') {
        p->at++;
    } else {
        for (;;) {
            skip_space(p);
            doe_json_member member = {0};
            if (p->at >= p->size || p->text[p->at] != '"' || !read_string(p, &member.key, &member.key_size)) {
                free(members.items);
                return fail(p, DOE_JSON_SYNTAX);
            }
            for (size_t i = 0; i < members.count; i++) {
                const doe_json_member *seen = (const doe_json_member *)members.items + i;
                if (seen->key_size == member.key_size && memcmp(seen->key, member.key, member.key_size) == 0) {
                    free(members.items);
                    return fail(p, DOE_JSON_DUPLICATE_KEY);
                }
            }
            skip_space(p);
            if (p->at >= p->size || p->text[p->at] != ':') { free(members.items); return fail(p, DOE_JSON_SYNTAX); }
            p->at++;
            member.value = read_value(p, depth + 1);
            if (!member.value) { free(members.items); return false; }
            if (!grow_push(&members, &member)) { free(members.items); return fail(p, DOE_JSON_OUT_OF_MEMORY); }
            skip_space(p);
            if (p->at < p->size && p->text[p->at] == ',') { p->at++; continue; }
            if (p->at < p->size && p->text[p->at] == '}') { p->at++; break; }
            free(members.items);
            return fail(p, DOE_JSON_SYNTAX);
        }
    }
    value->kind = DOE_JSON_OBJECT;
    value->count = members.count;
    value->members = settle(p, &members);
    return value->members ? true : fail(p, DOE_JSON_OUT_OF_MEMORY);
}

static doe_json_value *read_value(parser *p, unsigned depth)
{
    if (depth > DOE_JSON_MAX_DEPTH) { fail(p, DOE_JSON_DEPTH); return NULL; }
    skip_space(p);
    if (p->at >= p->size) { fail(p, DOE_JSON_SYNTAX); return NULL; }
    doe_json_value *value = own(p, sizeof(*value));
    if (!value) { fail(p, DOE_JSON_OUT_OF_MEMORY); return NULL; }
    bool ok;
    switch (p->text[p->at]) {
    case '{': ok = read_object(p, value, depth); break;
    case '[': ok = read_array(p, value, depth); break;
    case '"':
        value->kind = DOE_JSON_STRING;
        ok = read_string(p, &value->string, &value->string_size);
        break;
    case 't': value->kind = DOE_JSON_BOOL; value->boolean = true; ok = literal(p, "true"); break;
    case 'f': value->kind = DOE_JSON_BOOL; ok = literal(p, "false"); break;
    case 'n': value->kind = DOE_JSON_NULL; ok = literal(p, "null"); break;
    default: ok = read_number(p, value); break;
    }
    return ok ? value : NULL;
}

doe_json_status doe_json_parse(const char *text, size_t size, doe_json_document *document, doe_json_error *error)
{
    doe_json_document parsed = {0};
    parser p = {.text = text, .size = size, .document = &parsed, .status = DOE_JSON_OK};
    if (!text && size) p.status = DOE_JSON_SYNTAX;
    doe_json_value *root = p.status == DOE_JSON_OK ? read_value(&p, 0) : NULL;
    if (root) {
        skip_space(&p);
        if (p.at != size) fail(&p, DOE_JSON_SYNTAX);
    }
    if (error) *error = (doe_json_error){.status = p.status, .offset = p.at};
    if (p.status != DOE_JSON_OK) {
        doe_json_free(&parsed);
        *document = (doe_json_document){0};
        return p.status;
    }
    parsed.root = root;
    *document = parsed;
    return DOE_JSON_OK;
}

void doe_json_free(doe_json_document *document)
{
    for (size_t i = 0; i < document->allocation_count; i++) free(document->allocations[i]);
    free(document->allocations);
    *document = (doe_json_document){0};
}

const doe_json_value *doe_json_member_value(const doe_json_value *object, const char *key)
{
    if (!object || object->kind != DOE_JSON_OBJECT) return NULL;
    size_t length = strlen(key);
    for (size_t i = 0; i < object->count; i++) {
        const doe_json_member *member = &object->members[i];
        if (member->key_size == length && memcmp(member->key, key, length) == 0) return member->value;
    }
    return NULL;
}

bool doe_json_as_u64(const doe_json_value *value, uint64_t max, uint64_t *out)
{
    if (!value || value->kind != DOE_JSON_NUMBER || !value->integer) return false;
    if (value->negative && value->magnitude != 0) return false;
    if (value->magnitude > max) return false;
    *out = value->magnitude;
    return true;
}

const char *doe_json_status_name(doe_json_status status)
{
    switch (status) {
    case DOE_JSON_OK: return "ok";
    case DOE_JSON_SYNTAX: return "syntax error";
    case DOE_JSON_DEPTH: return "nesting deeper than the reader's bound";
    case DOE_JSON_DUPLICATE_KEY: return "duplicate object key";
    case DOE_JSON_OUT_OF_MEMORY: return "out of memory";
    }
    return "unknown";
}
