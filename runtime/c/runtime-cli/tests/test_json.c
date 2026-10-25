/* SPDX-License-Identifier: MIT */
#include "json.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static doe_json_status parse(const char *text, doe_json_document *document)
{
    doe_json_error error;
    return doe_json_parse(text, strlen(text), document, &error);
}

static void rejects(const char *text, doe_json_status expected)
{
    doe_json_document document;
    doe_json_status status = parse(text, &document);
    if (status != expected) fprintf(stderr, "%s: %s\n", text, doe_json_status_name(status));
    assert(status == expected);
    assert(document.root == NULL && document.allocations == NULL);
}

int main(void)
{
    doe_json_document document;
    assert(parse(" {\"a\": [1, -0, 18446744073709551615, 18446744073709551616, 2.5e3, true, null],"
        " \"s\": \"x\\\"\\\\\\/\\n\\u00e9\\ud83d\\ude00\"} ", &document) == DOE_JSON_OK);
    const doe_json_value *a = doe_json_member_value(document.root, "a");
    assert(a && a->kind == DOE_JSON_ARRAY && a->count == 7);
    uint64_t value;
    assert(doe_json_as_u64(a->items[0], 1, &value) && value == 1);
    assert(!doe_json_as_u64(a->items[0], 0, &value));
    assert(doe_json_as_u64(a->items[1], 0, &value) && value == 0); /* -0 is zero */
    assert(doe_json_as_u64(a->items[2], UINT64_MAX, &value) && value == UINT64_MAX);
    assert(!doe_json_as_u64(a->items[3], UINT64_MAX, &value) && !a->items[3]->integer);
    assert(!doe_json_as_u64(a->items[4], UINT64_MAX, &value) && a->items[4]->number == 2500.0);
    assert(a->items[5]->kind == DOE_JSON_BOOL && a->items[5]->boolean);
    assert(a->items[6]->kind == DOE_JSON_NULL);
    const doe_json_value *s = doe_json_member_value(document.root, "s");
    assert(s && s->kind == DOE_JSON_STRING);
    assert(s->string_size == 11 && !memcmp(s->string, "x\"\\/\n\xc3\xa9\xf0\x9f\x98\x80", 11));
    assert(!doe_json_member_value(document.root, "missing"));
    doe_json_free(&document);

    rejects("", DOE_JSON_SYNTAX);
    rejects("[1,]", DOE_JSON_SYNTAX);
    rejects("{\"a\":1,}", DOE_JSON_SYNTAX);
    rejects("[1] x", DOE_JSON_SYNTAX);
    rejects("01", DOE_JSON_SYNTAX);
    rejects("1.", DOE_JSON_SYNTAX);
    rejects("\"tab\there\"", DOE_JSON_SYNTAX);
    rejects("\"\\x\"", DOE_JSON_SYNTAX);
    rejects("\"\\ud800\"", DOE_JSON_SYNTAX);
    rejects("\"unterminated", DOE_JSON_SYNTAX);
    rejects("{\"k\":1,\"k\":2}", DOE_JSON_DUPLICATE_KEY);
    rejects("tru", DOE_JSON_SYNTAX);
    char deep[2 * DOE_JSON_MAX_DEPTH + 8];
    size_t depth = DOE_JSON_MAX_DEPTH + 2;
    memset(deep, '[', depth);
    memset(deep + depth, ']', depth);
    deep[2 * depth] = '\0';
    rejects(deep, DOE_JSON_DEPTH);
    puts("runtime-cli json: values, exact integers, escapes, UTF-16 pairs and 13 rejections (host only)");
    return 0;
}
