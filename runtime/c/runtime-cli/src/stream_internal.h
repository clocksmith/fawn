/* SPDX-License-Identifier: MIT
 * Field readers shared by the command parsers (stream.c, stream_texture.c).
 * Each returns false after filling `error` with the command index. */
#ifndef DOE_RUNTIME_STREAM_INTERNAL_H
#define DOE_RUNTIME_STREAM_INTERNAL_H
#include "stream.h"

bool stream_reject(doe_stream_error *error, size_t index, const char *format, ...)
    __attribute__((format(printf, 3, 4)));
/* Every member must be one of `allowed` (NULL-terminated). */
bool stream_only_members(const doe_json_value *object, const char *const *allowed, size_t index,
    const char *what, doe_stream_error *error);
/* The first present of two spellings; both present is ambiguous. */
const doe_json_value *stream_either(const doe_json_value *object, const char *a, const char *b, bool *ambiguous);
/* One of up to three spellings (NULL names are skipped); more than one present is ambiguous. */
const doe_json_value *stream_one_of(const doe_json_value *object, const char *a, const char *b, const char *c,
    bool *ambiguous);
bool stream_read_u64(const doe_json_value *object, const char *a, const char *b, uint64_t max, bool required,
    uint64_t fallback, uint64_t *out, size_t index, doe_stream_error *error);
bool stream_read_string(const doe_json_value *object, const char *a, const char *b, const char **out,
    size_t index, doe_stream_error *error);

bool stream_parse_copy(const doe_json_value *object, const char *name, doe_command *command, size_t index,
    doe_stream_error *error);
bool stream_parse_texture_write(const doe_json_value *object, doe_command *command, size_t index,
    doe_stream_error *error);
bool stream_parse_texture_query(const doe_json_value *object, doe_command *command, size_t index,
    doe_stream_error *error);
bool stream_parse_texture_destroy(const doe_json_value *object, doe_command *command, size_t index,
    doe_stream_error *error);
bool stream_parse_render(const doe_json_value *object, const char *name, doe_command *command, size_t index,
    doe_stream_error *error);
bool stream_parse_sampler(const doe_json_value *object, doe_command *command, size_t index, doe_stream_error *error);
bool stream_parse_sampler_destroy(const doe_json_value *object, doe_command *command, size_t index,
    doe_stream_error *error);
/* The texture format named `name`, or false. */
bool stream_texture_format(const char *name, doe_texture_format *out);
#endif
