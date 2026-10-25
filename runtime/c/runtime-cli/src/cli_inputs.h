/* SPDX-License-Identifier: MIT */
#ifndef DOE_C_CLI_INPUTS_H
#define DOE_C_CLI_INPUTS_H
#include "stream.h"
#include "quirks.h"

const char *cli_default_commands(void);
const char *cli_default_quirks(void);
/* Checks a bounded JSONL replay against the complete quirk-dispatched stream
 * before any device execution. Borrowed input; no references survive return. */
bool cli_validate_replay(const char *bytes, size_t size, const char *module, const doe_stream *stream,
    uint64_t repeats, const doe_quirk_set *quirks, char *message, size_t capacity);
#endif
