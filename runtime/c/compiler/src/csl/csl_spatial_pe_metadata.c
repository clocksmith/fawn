// SPDX-License-Identifier: MIT
// Port of runtime/zig/src/spatial/csl/csl_pe_program_metadata.zig (UNTESTED).
// Line-oriented, conservative parser. Parity NOT verified.
#include "csl_spatial_pe_metadata.h"

#include <string.h>

typedef struct Cursor {
    const char *line;
    size_t len;
    size_t idx;
} Cursor;

static int is_ident_start(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

static int is_ident(char c) {
    return is_ident_start(c) || (c >= '0' && c <= '9');
}

static int is_space_tab(char c) {
    return c == ' ' || c == '\t';
}

static void skip_spaces(Cursor *cur) {
    while (cur->idx < cur->len && is_space_tab(cur->line[cur->idx])) {
        cur->idx++;
    }
}

static int match_keyword(Cursor *cur, const char *keyword) {
    size_t klen = strlen(keyword);
    size_t after;
    if (cur->idx + klen > cur->len) {
        return 0;
    }
    if (memcmp(cur->line + cur->idx, keyword, klen) != 0) {
        return 0;
    }
    after = cur->idx + klen;
    if (after < cur->len && is_ident(cur->line[after])) {
        return 0;
    }
    cur->idx = after;
    return 1;
}

static int starts_with_at(Cursor *cur, const char *word) {
    size_t wlen = strlen(word);
    if (cur->idx + wlen > cur->len) {
        return 0;
    }
    return memcmp(cur->line + cur->idx, word, wlen) == 0;
}

static int read_ident(Cursor *cur, CslSpatialStr *out) {
    size_t start;
    if (cur->idx >= cur->len || !is_ident_start(cur->line[cur->idx])) {
        return 0;
    }
    start = cur->idx;
    while (cur->idx < cur->len && is_ident(cur->line[cur->idx])) {
        cur->idx++;
    }
    out->data = cur->line + start;
    out->len = cur->idx - start;
    return 1;
}

static int match_char(Cursor *cur, char c) {
    if (cur->idx >= cur->len || cur->line[cur->idx] != c) {
        return 0;
    }
    cur->idx++;
    return 1;
}

static CslSpatialStr trim_view(const char *data, size_t len) {
    size_t start = 0;
    size_t end = len;
    CslSpatialStr out;
    while (start < end && is_space_tab(data[start])) {
        start++;
    }
    while (end > start && is_space_tab(data[end - 1])) {
        end--;
    }
    out.data = data + start;
    out.len = end - start;
    return out;
}

/* Read "[...]" and return the trimmed inner text, or 0 when malformed. */
static int read_bracketed_expr(Cursor *cur, CslSpatialStr *out) {
    size_t start;
    size_t end;
    if (cur->idx >= cur->len || cur->line[cur->idx] != '[') {
        return 0;
    }
    cur->idx++;
    start = cur->idx;
    while (cur->idx < cur->len && cur->line[cur->idx] != ']') {
        cur->idx++;
    }
    if (cur->idx >= cur->len) {
        return 0;
    }
    end = cur->idx;
    cur->idx++;
    *out = trim_view(cur->line + start, end - start);
    return 1;
}

static void parse_var_typed_line(const char *line, size_t len,
                                 CslSpatialPeParsed *parsed) {
    Cursor cur;
    CslSpatialStr name;
    CslSpatialStr elem;
    CslSpatialStr backing;
    CslSpatialStr size_expr;
    size_t size_start;
    cur.line = line;
    cur.len = len;
    cur.idx = 0;
    skip_spaces(&cur);
    if (!match_keyword(&cur, "var")) {
        return;
    }
    skip_spaces(&cur);
    if (!read_ident(&cur, &name)) {
        return;
    }
    skip_spaces(&cur);
    if (!match_char(&cur, ':')) {
        return;
    }
    skip_spaces(&cur);
    if (cur.idx >= cur.len || cur.line[cur.idx] != '[') {
        return;
    }
    cur.idx++;
    skip_spaces(&cur);
    if (cur.idx < cur.len && cur.line[cur.idx] == '*') {
        cur.idx++;
        skip_spaces(&cur);
        if (!match_char(&cur, ']')) {
            return;
        }
        skip_spaces(&cur);
        if (!read_ident(&cur, &elem)) {
            return;
        }
        skip_spaces(&cur);
        if (!match_char(&cur, '=')) {
            return;
        }
        skip_spaces(&cur);
        if (!match_char(&cur, '&')) {
            return;
        }
        skip_spaces(&cur);
        if (!read_ident(&cur, &backing)) {
            return;
        }
        if (parsed->pointer_count >= CSL_SPATIAL_PE_MAX_POINTERS) {
            return;
        }
        parsed->pointers[parsed->pointer_count].name = name;
        parsed->pointers[parsed->pointer_count].backing = backing;
        parsed->pointers[parsed->pointer_count].elem_type = elem;
        parsed->pointer_count++;
        return;
    }
    size_start = cur.idx;
    while (cur.idx < cur.len && cur.line[cur.idx] != ']') {
        cur.idx++;
    }
    if (cur.idx >= cur.len) {
        return;
    }
    size_expr = trim_view(cur.line + size_start, cur.idx - size_start);
    cur.idx++;
    skip_spaces(&cur);
    if (!read_ident(&cur, &elem)) {
        return;
    }
    if (parsed->decl_count >= CSL_SPATIAL_PE_MAX_DECLS) {
        return;
    }
    parsed->decls[parsed->decl_count].name = name;
    parsed->decls[parsed->decl_count].size_expr = size_expr;
    parsed->decls[parsed->decl_count].elem_type = elem;
    parsed->decl_count++;
}

static void parse_var_zeros_line(const char *line, size_t len,
                                 CslSpatialPeParsed *parsed) {
    Cursor cur;
    CslSpatialStr name;
    CslSpatialStr size_expr;
    CslSpatialStr elem;
    size_t i;
    cur.line = line;
    cur.len = len;
    cur.idx = 0;
    skip_spaces(&cur);
    if (!match_keyword(&cur, "var")) {
        return;
    }
    skip_spaces(&cur);
    if (!read_ident(&cur, &name)) {
        return;
    }
    skip_spaces(&cur);
    if (!match_char(&cur, '=')) {
        return;
    }
    skip_spaces(&cur);
    if (!starts_with_at(&cur, "@zeros")) {
        return;
    }
    cur.idx += strlen("@zeros");
    skip_spaces(&cur);
    if (!match_char(&cur, '(')) {
        return;
    }
    skip_spaces(&cur);
    if (!read_bracketed_expr(&cur, &size_expr)) {
        return;
    }
    skip_spaces(&cur);
    if (!read_ident(&cur, &elem)) {
        return;
    }
    if (parsed->decl_count >= CSL_SPATIAL_PE_MAX_DECLS) {
        return;
    }
    for (i = 0; i < parsed->decl_count; i++) {
        if (parsed->decls[i].name.len == name.len &&
            memcmp(parsed->decls[i].name.data, name.data, name.len) == 0) {
            return;
        }
    }
    parsed->decls[parsed->decl_count].name = name;
    parsed->decls[parsed->decl_count].size_expr = size_expr;
    parsed->decls[parsed->decl_count].elem_type = elem;
    parsed->decl_count++;
}

static void parse_export_symbol_line(const char *line, size_t len,
                                     CslSpatialPeParsed *parsed) {
    Cursor cur;
    CslSpatialStr ptr;
    CslSpatialStr symbol;
    size_t sym_start;
    cur.line = line;
    cur.len = len;
    cur.idx = 0;
    skip_spaces(&cur);
    if (!starts_with_at(&cur, "@export_symbol")) {
        return;
    }
    cur.idx += strlen("@export_symbol");
    skip_spaces(&cur);
    if (!match_char(&cur, '(')) {
        return;
    }
    skip_spaces(&cur);
    if (!read_ident(&cur, &ptr)) {
        return;
    }
    skip_spaces(&cur);
    if (!match_char(&cur, ',')) {
        return;
    }
    skip_spaces(&cur);
    if (!match_char(&cur, '"')) {
        return;
    }
    sym_start = cur.idx;
    while (cur.idx < cur.len && cur.line[cur.idx] != '"') {
        cur.idx++;
    }
    if (cur.idx >= cur.len) {
        return;
    }
    symbol.data = cur.line + sym_start;
    symbol.len = cur.idx - sym_start;
    if (parsed->export_count >= CSL_SPATIAL_PE_MAX_EXPORTS) {
        return;
    }
    parsed->exports[parsed->export_count].symbol = symbol;
    parsed->exports[parsed->export_count].pointer = ptr;
    parsed->export_count++;
}

static void parse_constant_line(const char *line, size_t len,
                                CslSpatialPeParsed *parsed) {
    Cursor cur;
    CslSpatialStr kind;
    CslSpatialStr name;
    CslSpatialStr type_name;
    CslSpatialStr expr;
    size_t expr_start;
    size_t expr_end;
    cur.line = line;
    cur.len = len;
    cur.idx = 0;
    skip_spaces(&cur);
    if (match_keyword(&cur, "const")) {
        kind.data = "const";
        kind.len = 5;
    } else if (match_keyword(&cur, "param")) {
        kind.data = "param";
        kind.len = 5;
    } else {
        return;
    }
    skip_spaces(&cur);
    if (!read_ident(&cur, &name)) {
        return;
    }
    skip_spaces(&cur);
    if (!match_char(&cur, ':')) {
        return;
    }
    skip_spaces(&cur);
    if (!read_ident(&cur, &type_name)) {
        return;
    }
    skip_spaces(&cur);
    /* A param with no default is a kernel input and is not recorded. */
    if (!match_char(&cur, '=')) {
        return;
    }
    skip_spaces(&cur);
    expr_start = cur.idx;
    expr_end = cur.idx;
    while (cur.idx < cur.len && cur.line[cur.idx] != ';') {
        if (!is_space_tab(cur.line[cur.idx])) {
            expr_end = cur.idx + 1;
        }
        cur.idx++;
    }
    if (expr_end == expr_start) {
        return;
    }
    if (parsed->constant_count >= CSL_SPATIAL_PE_MAX_CONSTANTS) {
        return;
    }
    expr.data = cur.line + expr_start;
    expr.len = expr_end - expr_start;
    parsed->constants[parsed->constant_count].kind = kind;
    parsed->constants[parsed->constant_count].name = name;
    parsed->constants[parsed->constant_count].type_name = type_name;
    parsed->constants[parsed->constant_count].expr = expr;
    parsed->constant_count++;
}

static const char *classify_layout_kind(CslSpatialStr type_str) {
    CslSpatialStr trimmed = trim_view(type_str.data, type_str.len);
    size_t i;
    if (trimmed.len >= 2 && trimmed.data[0] == 'f' && trimmed.data[1] == 'n') {
        if (trimmed.len == 2 || !is_ident(trimmed.data[2])) {
            return "device_function";
        }
    }
    for (i = 0; i + 3 <= trimmed.len; i++) {
        if (trimmed.data[i] == 'f' && trimmed.data[i + 1] == 'n' &&
            trimmed.data[i + 2] == '(') {
            return "device_function";
        }
    }
    return "device_variable";
}

static void parse_layout_export_line(const char *line, size_t len,
                                     CslSpatialLayoutParsed *parsed) {
    Cursor cur;
    CslSpatialStr name;
    CslSpatialStr type_str;
    size_t name_start;
    size_t type_start;
    size_t type_end;
    int depth = 0;
    int mutable_flag = 0;
    cur.line = line;
    cur.len = len;
    cur.idx = 0;
    skip_spaces(&cur);
    if (!starts_with_at(&cur, "@export_name")) {
        return;
    }
    cur.idx += strlen("@export_name");
    skip_spaces(&cur);
    if (!match_char(&cur, '(')) {
        return;
    }
    skip_spaces(&cur);
    if (!match_char(&cur, '"')) {
        return;
    }
    name_start = cur.idx;
    while (cur.idx < cur.len && cur.line[cur.idx] != '"') {
        cur.idx++;
    }
    if (cur.idx >= cur.len) {
        return;
    }
    name.data = cur.line + name_start;
    name.len = cur.idx - name_start;
    cur.idx++;
    skip_spaces(&cur);
    if (!match_char(&cur, ',')) {
        return;
    }
    skip_spaces(&cur);
    type_start = cur.idx;
    type_end = cur.idx;
    while (cur.idx < cur.len) {
        char c = cur.line[cur.idx];
        if (depth == 0 && (c == ',' || c == ')')) {
            break;
        }
        if (c == '(') {
            depth++;
        }
        if (c == ')') {
            depth--;
        }
        if (!is_space_tab(c)) {
            type_end = cur.idx + 1;
        }
        cur.idx++;
    }
    if (type_end == type_start) {
        return;
    }
    if (cur.idx < cur.len && cur.line[cur.idx] == ',') {
        cur.idx++;
        skip_spaces(&cur);
        if (match_keyword(&cur, "true")) {
            mutable_flag = 1;
        } else if (match_keyword(&cur, "false")) {
            mutable_flag = 0;
        }
    }
    if (parsed->export_count >= CSL_SPATIAL_PE_MAX_LAYOUT_EXPORTS) {
        return;
    }
    type_str.data = cur.line + type_start;
    type_str.len = type_end - type_start;
    parsed->exports[parsed->export_count].name = name;
    parsed->exports[parsed->export_count].type_str = type_str;
    parsed->exports[parsed->export_count].kind.data = classify_layout_kind(type_str);
    parsed->exports[parsed->export_count].kind.len = strlen(parsed->exports[parsed->export_count].kind.data);
    parsed->exports[parsed->export_count].mutable_flag = mutable_flag;
    parsed->export_count++;
}

static void parse_lines(const char *source, size_t len,
                        CslSpatialPeParsed *parsed) {
    size_t start = 0;
    while (start < len) {
        size_t end = start;
        while (end < len && source[end] != '\n') {
            end++;
        }
        parse_var_typed_line(source + start, end - start, parsed);
        parse_var_zeros_line(source + start, end - start, parsed);
        parse_export_symbol_line(source + start, end - start, parsed);
        parse_constant_line(source + start, end - start, parsed);
        if (end >= len) {
            break;
        }
        start = end + 1;
    }
}

static void parse_layout_lines(const char *source, size_t len,
                               CslSpatialLayoutParsed *parsed) {
    size_t start = 0;
    while (start < len) {
        size_t end = start;
        while (end < len && source[end] != '\n') {
            end++;
        }
        parse_layout_export_line(source + start, end - start, parsed);
        if (end >= len) {
            break;
        }
        start = end + 1;
    }
}

CslErr csl_spatial_pe_parse(const char *source, size_t len,
                            CslSpatialPeParsed *out) {
    if (!out) {
        return CSL_ERR_INVALID;
    }
    if (len > 0 && !source) {
        return CSL_ERR_INVALID;
    }
    memset(out, 0, sizeof(*out));
    parse_lines(source, len, out);
    return CSL_OK;
}

CslErr csl_spatial_pe_parse_layout(const char *source, size_t len,
                                   CslSpatialLayoutParsed *out) {
    if (!out) {
        return CSL_ERR_INVALID;
    }
    if (len > 0 && !source) {
        return CSL_ERR_INVALID;
    }
    memset(out, 0, sizeof(*out));
    parse_layout_lines(source, len, out);
    return CSL_OK;
}

/* JSON string writer matching Zig writeJsonString: only the five */
/* escapes (" \\ \n \r \t) are escaped; every other byte is raw. */
static CslErr put_json_view(CslBuf *out, CslSpatialStr s) {
    CslErr err;
    size_t i;
    err = csl_buf_putc(out, '"');
    if (err) {
        return err;
    }
    for (i = 0; i < s.len; i++) {
        char c = s.data[i];
        const char *esc = NULL;
        if (c == '"') {
            esc = "\\\"";
        } else if (c == '\\') {
            esc = "\\\\";
        } else if (c == '\n') {
            esc = "\\n";
        } else if (c == '\r') {
            esc = "\\r";
        } else if (c == '\t') {
            esc = "\\t";
        }
        if (esc) {
            err = csl_buf_puts(out, esc);
        } else {
            err = csl_buf_putc(out, c);
        }
        if (err) {
            return err;
        }
    }
    return csl_buf_putc(out, '"');
}

static CslErr put_json_cstr(CslBuf *out, const char *s) {
    CslSpatialStr view;
    if (!s) {
        return CSL_ERR_INVALID;
    }
    view.data = s;
    view.len = strlen(s);
    return put_json_view(out, view);
}

static const CslSpatialPeDecl *find_decl(const CslSpatialPeParsed *parsed,
                                         CslSpatialStr name) {
    size_t i;
    for (i = 0; i < parsed->decl_count; i++) {
        if (parsed->decls[i].name.len == name.len &&
            memcmp(parsed->decls[i].name.data, name.data, name.len) == 0) {
            return &parsed->decls[i];
        }
    }
    return NULL;
}

static const CslSpatialPePointer *find_pointer(
    const CslSpatialPeParsed *parsed, CslSpatialStr name) {
    size_t i;
    for (i = 0; i < parsed->pointer_count; i++) {
        if (parsed->pointers[i].name.len == name.len &&
            memcmp(parsed->pointers[i].name.data, name.data, name.len) == 0) {
            return &parsed->pointers[i];
        }
    }
    return NULL;
}

CslErr csl_spatial_pe_emit_json(const char *source, size_t len, CslBuf *out) {
    CslSpatialPeParsed parsed;
    CslErr err;
    size_t i;
    if (!out) {
        return CSL_ERR_INVALID;
    }
    err = csl_spatial_pe_parse(source, len, &parsed);
    if (err) {
        return err;
    }
    err = csl_buf_puts(out, "{\n  \"variables\": [");
    if (err) {
        return err;
    }
    for (i = 0; i < parsed.decl_count; i++) {
        if (i > 0) {
            err = csl_buf_putc(out, ',');
            if (err) {
                return err;
            }
        }
        err = csl_buf_puts(out, "\n    {\"name\":");
        if (err) {
            return err;
        }
        err = put_json_view(out, parsed.decls[i].name);
        if (err) {
            return err;
        }
        err = csl_buf_puts(out, ",\"sizeExpr\":");
        if (err) {
            return err;
        }
        err = put_json_view(out, parsed.decls[i].size_expr);
        if (err) {
            return err;
        }
        err = csl_buf_puts(out, ",\"elemType\":");
        if (err) {
            return err;
        }
        err = put_json_view(out, parsed.decls[i].elem_type);
        if (err) {
            return err;
        }
        err = csl_buf_putc(out, '}');
        if (err) {
            return err;
        }
    }
    if (parsed.decl_count > 0) {
        err = csl_buf_puts(out, "\n  ");
        if (err) {
            return err;
        }
    }
    err = csl_buf_puts(out, "],\n  \"pointers\": [");
    if (err) {
        return err;
    }
    for (i = 0; i < parsed.pointer_count; i++) {
        if (i > 0) {
            err = csl_buf_putc(out, ',');
            if (err) {
                return err;
            }
        }
        err = csl_buf_puts(out, "\n    {\"name\":");
        if (err) {
            return err;
        }
        err = put_json_view(out, parsed.pointers[i].name);
        if (err) {
            return err;
        }
        err = csl_buf_puts(out, ",\"backing\":");
        if (err) {
            return err;
        }
        err = put_json_view(out, parsed.pointers[i].backing);
        if (err) {
            return err;
        }
        err = csl_buf_puts(out, ",\"elemType\":");
        if (err) {
            return err;
        }
        err = put_json_view(out, parsed.pointers[i].elem_type);
        if (err) {
            return err;
        }
        err = csl_buf_putc(out, '}');
        if (err) {
            return err;
        }
    }
    if (parsed.pointer_count > 0) {
        err = csl_buf_puts(out, "\n  ");
        if (err) {
            return err;
        }
    }
    err = csl_buf_puts(out, "],\n  \"exports\": [");
    if (err) {
        return err;
    }
    for (i = 0; i < parsed.export_count; i++) {
        const CslSpatialPePointer *ptr;
        if (i > 0) {
            err = csl_buf_putc(out, ',');
            if (err) {
                return err;
            }
        }
        err = csl_buf_puts(out, "\n    {\"symbol\":");
        if (err) {
            return err;
        }
        err = put_json_view(out, parsed.exports[i].symbol);
        if (err) {
            return err;
        }
        err = csl_buf_puts(out, ",\"pointer\":");
        if (err) {
            return err;
        }
        err = put_json_view(out, parsed.exports[i].pointer);
        if (err) {
            return err;
        }
        ptr = find_pointer(&parsed, parsed.exports[i].pointer);
        if (ptr) {
            const CslSpatialPeDecl *decl;
            err = csl_buf_puts(out, ",\"backing\":");
            if (err) {
                return err;
            }
            err = put_json_view(out, ptr->backing);
            if (err) {
                return err;
            }
            decl = find_decl(&parsed, ptr->backing);
            if (decl) {
                err = csl_buf_puts(out, ",\"sizeExpr\":");
                if (err) {
                    return err;
                }
                err = put_json_view(out, decl->size_expr);
                if (err) {
                    return err;
                }
                err = csl_buf_puts(out, ",\"elemType\":");
                if (err) {
                    return err;
                }
                err = put_json_view(out, decl->elem_type);
                if (err) {
                    return err;
                }
            }
        }
        err = csl_buf_putc(out, '}');
        if (err) {
            return err;
        }
    }
    if (parsed.export_count > 0) {
        err = csl_buf_puts(out, "\n  ");
        if (err) {
            return err;
        }
    }
    err = csl_buf_puts(out, "],\n  \"compileTimeConstants\": [");
    if (err) {
        return err;
    }
    for (i = 0; i < parsed.constant_count; i++) {
        if (i > 0) {
            err = csl_buf_putc(out, ',');
            if (err) {
                return err;
            }
        }
        err = csl_buf_puts(out, "\n    {\"kind\":");
        if (err) {
            return err;
        }
        err = put_json_view(out, parsed.constants[i].kind);
        if (err) {
            return err;
        }
        err = csl_buf_puts(out, ",\"name\":");
        if (err) {
            return err;
        }
        err = put_json_view(out, parsed.constants[i].name);
        if (err) {
            return err;
        }
        err = csl_buf_puts(out, ",\"type\":");
        if (err) {
            return err;
        }
        err = put_json_view(out, parsed.constants[i].type_name);
        if (err) {
            return err;
        }
        err = csl_buf_puts(out, ",\"expr\":");
        if (err) {
            return err;
        }
        err = put_json_view(out, parsed.constants[i].expr);
        if (err) {
            return err;
        }
        err = csl_buf_putc(out, '}');
        if (err) {
            return err;
        }
    }
    if (parsed.constant_count > 0) {
        err = csl_buf_puts(out, "\n  ");
        if (err) {
            return err;
        }
    }
    return csl_buf_puts(out, "]\n}\n");
}

CslErr csl_spatial_pe_emit_layout_json(const char *source, size_t len,
                                       CslBuf *out) {
    CslSpatialLayoutParsed parsed;
    CslErr err;
    size_t i;
    if (!out) {
        return CSL_ERR_INVALID;
    }
    err = csl_spatial_pe_parse_layout(source, len, &parsed);
    if (err) {
        return err;
    }
    err = csl_buf_puts(out, "{\n  \"exports\": [");
    if (err) {
        return err;
    }
    for (i = 0; i < parsed.export_count; i++) {
        if (i > 0) {
            err = csl_buf_putc(out, ',');
            if (err) {
                return err;
            }
        }
        err = csl_buf_puts(out, "\n    {\"name\":");
        if (err) {
            return err;
        }
        err = put_json_view(out, parsed.exports[i].name);
        if (err) {
            return err;
        }
        err = csl_buf_puts(out, ",\"type\":");
        if (err) {
            return err;
        }
        err = put_json_view(out, parsed.exports[i].type_str);
        if (err) {
            return err;
        }
        err = csl_buf_puts(out, ",\"kind\":");
        if (err) {
            return err;
        }
        err = put_json_view(out, parsed.exports[i].kind);
        if (err) {
            return err;
        }
        err = csl_buf_puts(out, ",\"mutable\":");
        if (err) {
            return err;
        }
        err = csl_buf_puts(out, parsed.exports[i].mutable_flag ? "true" : "false");
        if (err) {
            return err;
        }
        err = csl_buf_putc(out, '}');
        if (err) {
            return err;
        }
    }
    if (parsed.export_count > 0) {
        err = csl_buf_puts(out, "\n  ");
        if (err) {
            return err;
        }
    }
    return csl_buf_puts(out, "]\n}\n");
}

CslErr csl_spatial_pe_emit_targets_json(
    const CslSpatialTargetDescriptor *targets, size_t count, CslBuf *out) {
    CslErr err;
    size_t i;
    if (!out) {
        return CSL_ERR_INVALID;
    }
    if (count > 0 && !targets) {
        return CSL_ERR_INVALID;
    }
    err = csl_buf_puts(out, "{\n  \"targets\": [");
    if (err) {
        return err;
    }
    for (i = 0; i < count; i++) {
        if (!targets[i].name || !targets[i].base_kernel ||
            !targets[i].layout || !targets[i].pe_program) {
            return CSL_ERR_INVALID;
        }
        if (i > 0) {
            err = csl_buf_putc(out, ',');
            if (err) {
                return err;
            }
        }
        err = csl_buf_puts(out, "\n    {\"name\":");
        if (err) {
            return err;
        }
        err = put_json_cstr(out, targets[i].name);
        if (err) {
            return err;
        }
        err = csl_buf_puts(out, ",\"baseKernel\":");
        if (err) {
            return err;
        }
        err = put_json_cstr(out, targets[i].base_kernel);
        if (err) {
            return err;
        }
        err = csl_buf_puts(out, ",\"phase\":");
        if (err) {
            return err;
        }
        if (targets[i].phase) {
            err = put_json_cstr(out, targets[i].phase);
        } else {
            err = csl_buf_puts(out, "null");
        }
        if (err) {
            return err;
        }
        err = csl_buf_puts(out, ",\"layout\":");
        if (err) {
            return err;
        }
        err = put_json_cstr(out, targets[i].layout);
        if (err) {
            return err;
        }
        err = csl_buf_puts(out, ",\"peProgram\":");
        if (err) {
            return err;
        }
        err = put_json_cstr(out, targets[i].pe_program);
        if (err) {
            return err;
        }
        err = csl_buf_putc(out, '}');
        if (err) {
            return err;
        }
    }
    if (count > 0) {
        err = csl_buf_puts(out, "\n  ");
        if (err) {
            return err;
        }
    }
    return csl_buf_puts(out, "]\n}\n");
}
