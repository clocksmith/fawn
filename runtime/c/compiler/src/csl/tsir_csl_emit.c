// SPDX-License-Identifier: MIT
#include "tsir_csl_emit.h"

#include "tsir_csl_kernel_body.h"

static CslErr write_hash_hex(CslBuf *out,
    const unsigned char hash[32]) {
    static const char hex[] = "0123456789abcdef";
    int i = 0;
    CslErr err = CSL_OK;
    for (i = 0; i < 32; i++) {
        char pair[3];
        pair[0] = hex[(hash[i] >> 4) & 0x0f];
        pair[1] = hex[hash[i] & 0x0f];
        pair[2] = '\0';
        err = csl_buf_puts(out, pair);
        if (err != CSL_OK) {
            return err;
        }
    }
    return CSL_OK;
}

/* Zig writeQuoted: escapes only backslash, double quote, \n, \r,
 * \t; every other byte is written verbatim. This intentionally
 * does NOT use csl_buf_put_json_escaped, which additionally
 * escapes other control bytes as \uXXXX and would change the
 * contract header for such input. */
static CslErr write_quoted(CslBuf *out, const char *text) {
    CslErr err = CSL_OK;
    const unsigned char *p = NULL;
    if ((err = csl_buf_putc(out, '"')) != CSL_OK) {
        return err;
    }
    if (text != NULL) {
        for (p = (const unsigned char *)text; *p != '\0'; p++) {
            if (*p == '\\') {
                err = csl_buf_puts(out, "\\\\");
            } else if (*p == '"') {
                err = csl_buf_puts(out, "\\\"");
            } else if (*p == '\n') {
                err = csl_buf_puts(out, "\\n");
            } else if (*p == '\r') {
                err = csl_buf_puts(out, "\\r");
            } else if (*p == '\t') {
                err = csl_buf_puts(out, "\\t");
            } else {
                err = csl_buf_putc(out, (char)*p);
            }
            if (err != CSL_OK) {
                return err;
            }
        }
    }
    return csl_buf_putc(out, '"');
}

static CslErr write_contract_header(CslBuf *out,
    const TsirCslRealization *real) {
    CslErr err = CSL_OK;
    if ((err = csl_buf_puts(out,
        "// doe.tsir.csl_skeleton.version = 1\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out, "// target.name = %s\n",
        real->target_name)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out,
        "// target.descriptor_hash = ")) != CSL_OK) {
        return err;
    }
    err = write_hash_hex(out, real->descriptor_hash);
    if (err != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out, "\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out, "// semantic_index = %u\n",
        real->semantic_index)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out, "// pe_grid.width = %u\n",
        real->pe_grid_width)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out, "// pe_grid.height = %u\n",
        real->pe_grid_height)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out,
        "// emitter_params_json = ")) != CSL_OK) {
        return err;
    }
    err = write_quoted(out, real->emitter_params_json);
    if (err != CSL_OK) {
        return err;
    }
    return csl_buf_puts(out, "\n\n");
}

static CslErr write_metadata_block(CslBuf *out, const char *text,
    const char *empty_block) {
    if (text != NULL) {
        return csl_buf_puts(out, text);
    }
    return csl_buf_puts(out, empty_block);
}

static CslErr write_metadata(CslBuf *out,
    const TsirCslRealization *real) {
    CslErr err = CSL_OK;
    err = write_metadata_block(out, real->residency_text,
        "// residency.count = 0\n\n");
    if (err != CSL_OK) {
        return err;
    }
    err = write_metadata_block(out, real->tiles_text,
        "// tiles.count = 0\n\n");
    if (err != CSL_OK) {
        return err;
    }
    err = write_metadata_block(out, real->collectives_text,
        "// collectives.count = 0\n\n");
    if (err != CSL_OK) {
        return err;
    }
    return write_metadata_block(out, real->reductions_text,
        "// reductions.count = 0\n\n");
}

/* Shared layout tile loop: the skeleton and semantic layouts
 * differ only by the optional memcpy_params and kv_len tile-code
 * entries, which are data (flags), not different logic. This
 * helper writes "layout {" through the loop close "    }\n";
 * the caller then writes its export lines and the closing "}". */
static CslErr write_tile_loop(CslBuf *out, int with_memcpy,
    int with_kv_len) {
    CslErr err = CSL_OK;
    if ((err = csl_buf_puts(out, "layout {\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out,
        "    @set_rectangle(width, height);\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out,
        "    for (@range(u32, height)) |pe_y| {\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out,
        "        for (@range(u32, width)) |pe_x| {\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out,
        "            @set_tile_code(pe_x, pe_y, \"pe_program.csl\", .{\n")) != CSL_OK) {
        return err;
    }
    if (with_memcpy) {
        if ((err = csl_buf_puts(out,
            "                .memcpy_params = memcpy.get_params(pe_x),\n")) != CSL_OK) {
            return err;
        }
    }
    if (with_kv_len) {
        if ((err = csl_buf_puts(out,
            "                .kv_len = kv_len,\n")) != CSL_OK) {
            return err;
        }
    }
    if ((err = csl_buf_puts(out,
        "                .pe_id = pe_y * width + pe_x,\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out,
        "                .num_pes = width * height,\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out, "            });\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out, "        }\n")) != CSL_OK) {
        return err;
    }
    return csl_buf_puts(out, "    }\n");
}

/* Shared pe_program header: marker, contract header, metadata,
 * pe_id/num_pes params. Identical in skeleton and semantic
 * emission; only what follows differs (stub vs kernel body). */
static CslErr write_pe_program_header(CslBuf *out,
    const TsirCslRealization *real) {
    CslErr err = CSL_OK;
    if ((err = csl_buf_puts(out, "//--- pe_program.csl ---\n")) != CSL_OK) {
        return err;
    }
    err = write_contract_header(out, real);
    if (err != CSL_OK) {
        return err;
    }
    err = write_metadata(out, real);
    if (err != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out, "param pe_id: u32;\n")) != CSL_OK) {
        return err;
    }
    return csl_buf_puts(out, "param num_pes: u32;\n\n");
}

static CslErr validate_real(const TsirCslRealization *real) {
    size_t i = 0;
    if (real == NULL) {
        return CSL_ERR_INVALID;
    }
    if (real->target_name == NULL ||
        real->descriptor_hash == NULL) {
        return CSL_ERR_INVALID;
    }
    if (real->binding_count > 0 && real->bindings == NULL) {
        return CSL_ERR_INVALID;
    }
    for (i = 0; i < real->binding_count; i++) {
        if (real->bindings[i].name == NULL ||
            real->bindings[i].name[0] == '\0') {
            return CSL_ERR_INVALID;
        }
    }
    if (real->has_rejection) {
        return CSL_ERR_INVALID;
    }
    return CSL_OK;
}

CslErr tsir_csl_emit_skeleton(const TsirCslRealization *real,
    CslBuf *out) {
    CslErr err = CSL_OK;
    if (out == NULL) {
        return CSL_ERR_INVALID;
    }
    err = validate_real(real);
    if (err != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out, "//--- layout.csl ---\n")) != CSL_OK) {
        return err;
    }
    err = write_contract_header(out, real);
    if (err != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out, "param width: u32;\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out, "param height: u32;\n\n")) != CSL_OK) {
        return err;
    }
    err = write_tile_loop(out, 0, 0);
    if (err != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out,
        "    @export_name(\"compute\", fn()void);\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out, "}\n\n")) != CSL_OK) {
        return err;
    }
    err = write_pe_program_header(out, real);
    if (err != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out, "fn compute() void {\n")) != CSL_OK) {
        return err;
    }
    return csl_buf_puts(out,
        "    // tsir mechanical skeleton: kernel body is emitted by later lowering.\n}\n");
}

CslErr tsir_csl_emit_semantic(const TsirCslRealization *real,
    const TsirCslKernelDesc *desc, CslBuf *out) {
    CslErr err = CSL_OK;
    size_t i = 0;
    int is_attention = 0;
    if (out == NULL || desc == NULL) {
        return CSL_ERR_INVALID;
    }
    err = validate_real(real);
    if (err != CSL_OK) {
        return err;
    }
    is_attention = desc->op == TSIR_CSL_OP_ATTENTION_SCORES;
    if ((err = csl_buf_puts(out, "//--- layout.csl ---\n")) != CSL_OK) {
        return err;
    }
    err = write_contract_header(out, real);
    if (err != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out, "param width: u32;\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out, "param height: u32;\n")) != CSL_OK) {
        return err;
    }
    if (is_attention) {
        if ((err = csl_buf_puts(out,
            "param kv_len: i16;\n")) != CSL_OK) {
            return err;
        }
    }
    if ((err = csl_buf_puts(out, "\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out,
        "const memcpy = @import_module(\"<memcpy/get_params>\", .{\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out, "    .width = width,\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out, "    .height = height,\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out, "});\n\n")) != CSL_OK) {
        return err;
    }
    err = write_tile_loop(out, 1, is_attention);
    if (err != CSL_OK) {
        return err;
    }
    for (i = 0; i < real->binding_count; i++) {
        /* Zig hardcodes [*]f32 here regardless of elem; ported
         * faithfully, not corrected to the binding elem. */
        if ((err = csl_buf_printf(out,
            "    @export_name(\"%s\", [*]f32, true);\n",
            real->bindings[i].name)) != CSL_OK) {
            return err;
        }
    }
    if ((err = csl_buf_puts(out,
        "    @export_name(\"compute\", fn()void);\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out, "}\n\n")) != CSL_OK) {
        return err;
    }
    err = write_pe_program_header(out, real);
    if (err != CSL_OK) {
        return err;
    }
    return tsir_csl_emit_kernel_body(desc, out);
}

CslErr tsir_csl_emitter_code_digest(unsigned char out_hash[32]) {
    (void)out_hash;
    return CSL_ERR_UNSUPPORTED;
}
