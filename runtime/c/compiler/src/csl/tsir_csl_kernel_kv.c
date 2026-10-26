// SPDX-License-Identifier: MIT
#include "tsir_csl_kernel_body.h"

/* Port of emit_kernel_body_kv.zig (245 lines): slot-sharded CSL
 * bodies for kv_write and kv_read only.
 *
 * The full_per_pe KV bodies live in the excluded
 * emit_kernel_body.zig and are NOT re-ported: calling these entry
 * points with kv_cache_pe_strategy == full_per_pe returns
 * CSL_ERR_UNSUPPORTED, visibly, instead of emitting the full
 * bodies from the wrong file.
 *
 * Slot-sharded idea, exactly as in Zig: each PE owns
 * slots_per_pe slots of size head_dim along the position axis.
 * A write mutates its cache only when the global decode position
 * falls inside the local stride (owning_pe == pe_id); a read emits
 * the full local slice and the host plan stitches PE outputs into
 * the contiguous logical cache. Read_start / read_len from the
 * full_per_pe path are therefore NOT surfaced here; slicing is
 * host-side.
 * Dims come from config defaults (head_dim_default,
 * max_seq_len_default, kv_slots_per_pe_default), as in Zig, not
 * from body params. Layout-supplied CSL params: pe_id, num_pes,
 * and slots_per_pe when no config default exists. Note the
 * inherited Zig quirk: this header declares pe_id/num_pes as i16
 * even though the emit_csl skeleton declares pe_id as u32; ported
 * faithfully, not fixed here. */

static CslErr write_kv_header(CslBuf *out,
    const TsirCslConfig *config) {
    CslErr err = CSL_OK;
    if ((err = csl_buf_puts(out, "param memcpy_params;\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out, "param pe_id: i16;\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out, "param num_pes: i16;\n")) != CSL_OK) {
        return err;
    }
    if (config->has_head_dim_default) {
        if ((err = csl_buf_printf(out, "param head_dim: i16 = %u;\n",
            config->head_dim_default)) != CSL_OK) {
            return err;
        }
    } else {
        if ((err = csl_buf_puts(out,
            "param head_dim: i16;\n")) != CSL_OK) {
            return err;
        }
    }
    if (config->has_max_seq_len_default) {
        if ((err = csl_buf_printf(out, "param max_seq_len: i16 = %u;\n",
            config->max_seq_len_default)) != CSL_OK) {
            return err;
        }
    } else {
        if ((err = csl_buf_puts(out,
            "param max_seq_len: i16;\n")) != CSL_OK) {
            return err;
        }
    }
    if (config->has_kv_slots_per_pe_default) {
        if ((err = csl_buf_printf(out,
            "param slots_per_pe: i16 = %u;\n",
            config->kv_slots_per_pe_default)) != CSL_OK) {
            return err;
        }
    } else {
        if ((err = csl_buf_puts(out,
            "param slots_per_pe: i16;\n")) != CSL_OK) {
            return err;
        }
    }
    return tsir_csl_write_sys_mod(out);
}

static CslErr resolve_slot_sharded(const TsirCslKernelDesc *desc,
    TsirCslConfig *defaults, const TsirCslConfig **config) {
    CslErr err = CSL_OK;
    /* No separate NULL check here: tsir_csl_resolve_config already
     * returns CSL_ERR_INVALID for a NULL descriptor, so a second
     * check would be redundant dead code with the same result. */
    err = tsir_csl_resolve_config(desc, defaults, config);
    if (err != CSL_OK) {
        return err;
    }
    if ((*config)->kv_cache_pe_strategy !=
        TSIR_CSL_KV_SLOT_SHARDED) {
        return CSL_ERR_UNSUPPORTED;
    }
    return CSL_OK;
}

CslErr tsir_csl_emit_kv_write(const TsirCslKernelDesc *desc,
    CslBuf *out) {
    TsirCslConfig defaults;
    const TsirCslConfig *config = NULL;
    const TsirCslBinding *key_proj = NULL;
    const TsirCslBinding *val_proj = NULL;
    const TsirCslBinding *key_cache = NULL;
    const TsirCslBinding *val_cache = NULL;
    const TsirCslBinding *position = NULL;
    TsirCslElem elem = TSIR_CSL_F32;
    const char *p = NULL;
    const char *ty = NULL;
    CslErr err = CSL_OK;
    if (desc == NULL || out == NULL) {
        return CSL_ERR_INVALID;
    }
    err = resolve_slot_sharded(desc, &defaults, &config);
    if (err != CSL_OK) {
        return err;
    }
    err = tsir_csl_binding_for_role(desc, TSIR_CSL_ROLE_KEY_PROJECTION,
        &key_proj);
    if (err != CSL_OK) {
        return err;
    }
    err = tsir_csl_binding_for_role(desc,
        TSIR_CSL_ROLE_VALUE_PROJECTION, &val_proj);
    if (err != CSL_OK) {
        return err;
    }
    err = tsir_csl_binding_for_role(desc, TSIR_CSL_ROLE_KEY_CACHE,
        &key_cache);
    if (err != CSL_OK) {
        return err;
    }
    err = tsir_csl_binding_for_role(desc, TSIR_CSL_ROLE_VALUE_CACHE,
        &val_cache);
    if (err != CSL_OK) {
        return err;
    }
    err = tsir_csl_binding_for_role(desc, TSIR_CSL_ROLE_DECODE_POSITION,
        &position);
    if (err != CSL_OK) {
        return err;
    }
    elem = key_cache->elem;
    err = tsir_csl_require_supported_compute_elem(elem);
    if (err != CSL_OK) {
        return err;
    }
    err = tsir_csl_require_elem(key_proj, elem);
    if (err != CSL_OK) {
        return err;
    }
    err = tsir_csl_require_elem(val_proj, elem);
    if (err != CSL_OK) {
        return err;
    }
    err = tsir_csl_require_elem(key_cache, elem);
    if (err != CSL_OK) {
        return err;
    }
    err = tsir_csl_require_elem(val_cache, elem);
    if (err != CSL_OK) {
        return err;
    }
    err = tsir_csl_require_elem(position, TSIR_CSL_U32);
    if (err != CSL_OK) {
        return err;
    }
    p = config->var_prefix;
    ty = tsir_csl_elem_name(elem);
    if (ty == NULL) {
        return CSL_ERR_INVALID;
    }
    err = write_kv_header(out, config);
    if (err != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out,
        "const local_kv_len: u32 = @as(u32, slots_per_pe) * @as(u32, head_dim);\n")) != CSL_OK) {
        return err;
    }
    if ((err = tsir_csl_write_buffer_array(out, p, key_proj->name,
        "head_dim", ty)) != CSL_OK) {
        return err;
    }
    if ((err = tsir_csl_write_buffer_array(out, p, val_proj->name,
        "head_dim", ty)) != CSL_OK) {
        return err;
    }
    if ((err = tsir_csl_write_buffer_array(out, p, key_cache->name,
        "local_kv_len", ty)) != CSL_OK) {
        return err;
    }
    if ((err = tsir_csl_write_buffer_array(out, p, val_cache->name,
        "local_kv_len", ty)) != CSL_OK) {
        return err;
    }
    if ((err = tsir_csl_write_buffer_array(out, p, position->name,
        "1", "u32")) != CSL_OK) {
        return err;
    }
    if ((err = tsir_csl_write_buffer_pointer(out, p, key_proj->name,
        ty)) != CSL_OK) {
        return err;
    }
    if ((err = tsir_csl_write_buffer_pointer(out, p, val_proj->name,
        ty)) != CSL_OK) {
        return err;
    }
    if ((err = tsir_csl_write_buffer_pointer(out, p, key_cache->name,
        ty)) != CSL_OK) {
        return err;
    }
    if ((err = tsir_csl_write_buffer_pointer(out, p, val_cache->name,
        ty)) != CSL_OK) {
        return err;
    }
    if ((err = tsir_csl_write_buffer_pointer(out, p, position->name,
        "u32")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out, "\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out, "fn compute() void {\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out, "    const global_pos: u32 = %s%s[0];\n",
        p, position->name)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out,
        "    const owning_pe: u32 = global_pos / @as(u32, slots_per_pe);\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out,
        "    if (owning_pe == @as(u32, pe_id)) {\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out,
        "        const local_slot: u32 = global_pos - owning_pe * @as(u32, slots_per_pe);\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out,
        "        const base: u32 = local_slot * @as(u32, head_dim);\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out,
        "        for (@range(i16, head_dim)) |d| {\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out,
        "            const idx = base + @as(u32, d);\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out,
        "            %s%s[idx] = %s%s[@as(u32, d)];\n", p,
        key_cache->name, p, key_proj->name)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out,
        "            %s%s[idx] = %s%s[@as(u32, d)];\n", p,
        val_cache->name, p, val_proj->name)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out, "        }\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out, "    }\n")) != CSL_OK) {
        return err;
    }
    return tsir_csl_write_compute_tail(out, p,
        (const char *[]){key_proj->name, val_proj->name,
            key_cache->name, val_cache->name, position->name}, 5);
}

CslErr tsir_csl_emit_kv_read(const TsirCslKernelDesc *desc,
    CslBuf *out) {
    TsirCslConfig defaults;
    const TsirCslConfig *config = NULL;
    const TsirCslBinding *key_cache = NULL;
    const TsirCslBinding *val_cache = NULL;
    const TsirCslBinding *key_output = NULL;
    const TsirCslBinding *val_output = NULL;
    TsirCslElem elem = TSIR_CSL_F32;
    const char *p = NULL;
    const char *ty = NULL;
    CslErr err = CSL_OK;
    if (desc == NULL || out == NULL) {
        return CSL_ERR_INVALID;
    }
    err = resolve_slot_sharded(desc, &defaults, &config);
    if (err != CSL_OK) {
        return err;
    }
    err = tsir_csl_binding_for_role(desc, TSIR_CSL_ROLE_KEY_CACHE,
        &key_cache);
    if (err != CSL_OK) {
        return err;
    }
    err = tsir_csl_binding_for_role(desc, TSIR_CSL_ROLE_VALUE_CACHE,
        &val_cache);
    if (err != CSL_OK) {
        return err;
    }
    err = tsir_csl_binding_for_role(desc, TSIR_CSL_ROLE_KEY_OUTPUT,
        &key_output);
    if (err != CSL_OK) {
        return err;
    }
    err = tsir_csl_binding_for_role(desc, TSIR_CSL_ROLE_VALUE_OUTPUT,
        &val_output);
    if (err != CSL_OK) {
        return err;
    }
    elem = key_cache->elem;
    err = tsir_csl_require_supported_compute_elem(elem);
    if (err != CSL_OK) {
        return err;
    }
    err = tsir_csl_require_elem(key_cache, elem);
    if (err != CSL_OK) {
        return err;
    }
    err = tsir_csl_require_elem(val_cache, elem);
    if (err != CSL_OK) {
        return err;
    }
    err = tsir_csl_require_elem(key_output, elem);
    if (err != CSL_OK) {
        return err;
    }
    err = tsir_csl_require_elem(val_output, elem);
    if (err != CSL_OK) {
        return err;
    }
    p = config->var_prefix;
    ty = tsir_csl_elem_name(elem);
    if (ty == NULL) {
        return CSL_ERR_INVALID;
    }
    err = write_kv_header(out, config);
    if (err != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out,
        "const local_kv_len: u32 = @as(u32, slots_per_pe) * @as(u32, head_dim);\n")) != CSL_OK) {
        return err;
    }
    if ((err = tsir_csl_write_buffer_array(out, p, key_cache->name,
        "local_kv_len", ty)) != CSL_OK) {
        return err;
    }
    if ((err = tsir_csl_write_buffer_array(out, p, val_cache->name,
        "local_kv_len", ty)) != CSL_OK) {
        return err;
    }
    if ((err = tsir_csl_write_buffer_array(out, p, key_output->name,
        "local_kv_len", ty)) != CSL_OK) {
        return err;
    }
    if ((err = tsir_csl_write_buffer_array(out, p, val_output->name,
        "local_kv_len", ty)) != CSL_OK) {
        return err;
    }
    if ((err = tsir_csl_write_buffer_pointer(out, p, key_cache->name,
        ty)) != CSL_OK) {
        return err;
    }
    if ((err = tsir_csl_write_buffer_pointer(out, p, val_cache->name,
        ty)) != CSL_OK) {
        return err;
    }
    if ((err = tsir_csl_write_buffer_pointer(out, p, key_output->name,
        ty)) != CSL_OK) {
        return err;
    }
    if ((err = tsir_csl_write_buffer_pointer(out, p, val_output->name,
        ty)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out, "\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out, "fn compute() void {\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out,
        "    for (@range(i16, slots_per_pe)) |slot| {\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out,
        "        for (@range(i16, head_dim)) |d| {\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out,
        "            const idx: u32 = @as(u32, slot) * @as(u32, head_dim) + @as(u32, d);\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out,
        "            %s%s[idx] = %s%s[idx];\n", p, key_output->name,
        p, key_cache->name)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out,
        "            %s%s[idx] = %s%s[idx];\n", p, val_output->name,
        p, val_cache->name)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out, "        }\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out, "    }\n")) != CSL_OK) {
        return err;
    }
    return tsir_csl_write_compute_tail(out, p,
        (const char *[]){key_cache->name, val_cache->name,
            key_output->name, val_output->name}, 4);
}
