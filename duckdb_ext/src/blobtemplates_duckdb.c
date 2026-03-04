/*
 * DuckDB C API extension for Inja template rendering.
 *
 * Registers:
 *   template_render(template VARCHAR, json_data VARCHAR) -> VARCHAR
 *   template_render_with_options(template VARCHAR, json_data VARCHAR, options VARCHAR) -> VARCHAR
 *
 * Uses the simple C extension mechanism (DUCKDB_EXTENSION_ENTRYPOINT).
 */

#define DUCKDB_EXTENSION_NAME blobtemplates
#include "duckdb_extension.h"

#include "blobtemplates.h"

#include <string.h>

DUCKDB_EXTENSION_EXTERN

/* ── String helpers ───────────────────────────────────────────────── */

/* Return a pointer to the string data and its length.  No allocation. */
static const char *str_ptr(duckdb_string_t *s, uint32_t *out_len) {
    uint32_t len = s->value.inlined.length;
    *out_len = len;
    if (len <= 12) {
        return s->value.inlined.inlined;
    }
    return s->value.pointer.ptr;
}

/* ── 2-arg template_render ────────────────────────────────────────── */

static void template_render_func(duckdb_function_info info,
                                  duckdb_data_chunk input,
                                  duckdb_vector output) {
    idx_t size = duckdb_data_chunk_get_size(input);
    duckdb_vector tmpl_vec = duckdb_data_chunk_get_vector(input, 0);
    duckdb_vector data_vec = duckdb_data_chunk_get_vector(input, 1);

    duckdb_string_t *tmpl_data = (duckdb_string_t *)duckdb_vector_get_data(tmpl_vec);
    duckdb_string_t *data_data = (duckdb_string_t *)duckdb_vector_get_data(data_vec);

    uint64_t *tmpl_validity = duckdb_vector_get_validity(tmpl_vec);
    uint64_t *data_validity = duckdb_vector_get_validity(data_vec);

    for (idx_t row = 0; row < size; row++) {
        if ((tmpl_validity && !duckdb_validity_row_is_valid(tmpl_validity, row)) ||
            (data_validity && !duckdb_validity_row_is_valid(data_validity, row))) {
            duckdb_vector_ensure_validity_writable(output);
            duckdb_validity_set_row_invalid(duckdb_vector_get_validity(output), row);
            continue;
        }

        uint32_t tmpl_len, json_len;
        const char *tmpl = str_ptr(&tmpl_data[row], &tmpl_len);
        const char *json = str_ptr(&data_data[row], &json_len);

        char *result = blobtemplates_render_n(tmpl, tmpl_len, json, json_len);
        if (result) {
            duckdb_vector_assign_string_element(output, row, result);
            blobtemplates_free(result);
        } else {
            duckdb_scalar_function_set_error(info, blobtemplates_errmsg());
            return;
        }
    }
}

/* ── 3-arg template_render_with_options ──────────────────────────── */

static void template_render_opts_func(duckdb_function_info info,
                                       duckdb_data_chunk input,
                                       duckdb_vector output) {
    idx_t size = duckdb_data_chunk_get_size(input);
    duckdb_vector tmpl_vec = duckdb_data_chunk_get_vector(input, 0);
    duckdb_vector data_vec = duckdb_data_chunk_get_vector(input, 1);
    duckdb_vector opts_vec = duckdb_data_chunk_get_vector(input, 2);

    duckdb_string_t *tmpl_data = (duckdb_string_t *)duckdb_vector_get_data(tmpl_vec);
    duckdb_string_t *data_data = (duckdb_string_t *)duckdb_vector_get_data(data_vec);
    duckdb_string_t *opts_data = (duckdb_string_t *)duckdb_vector_get_data(opts_vec);

    uint64_t *tmpl_validity = duckdb_vector_get_validity(tmpl_vec);
    uint64_t *data_validity = duckdb_vector_get_validity(data_vec);
    uint64_t *opts_validity = duckdb_vector_get_validity(opts_vec);

    for (idx_t row = 0; row < size; row++) {
        if ((tmpl_validity && !duckdb_validity_row_is_valid(tmpl_validity, row)) ||
            (data_validity && !duckdb_validity_row_is_valid(data_validity, row)) ||
            (opts_validity && !duckdb_validity_row_is_valid(opts_validity, row))) {
            duckdb_vector_ensure_validity_writable(output);
            duckdb_validity_set_row_invalid(duckdb_vector_get_validity(output), row);
            continue;
        }

        uint32_t tmpl_len, json_len, opts_len;
        const char *tmpl = str_ptr(&tmpl_data[row], &tmpl_len);
        const char *json = str_ptr(&data_data[row], &json_len);
        const char *opts = str_ptr(&opts_data[row], &opts_len);

        char *result = blobtemplates_render_with_options_n(
            tmpl, tmpl_len, json, json_len, opts, opts_len);
        if (result) {
            duckdb_vector_assign_string_element(output, row, result);
            blobtemplates_free(result);
        } else {
            duckdb_scalar_function_set_error(info, blobtemplates_errmsg());
            return;
        }
    }
}

/* ── Helper: register scalar functions ───────────────────────────── */

static void register_functions(duckdb_connection connection) {
    duckdb_logical_type varchar_type = duckdb_create_logical_type(DUCKDB_TYPE_VARCHAR);

    /* 2-arg: template_render(template, json_data) */
    {
        duckdb_scalar_function func = duckdb_create_scalar_function();
        duckdb_scalar_function_set_name(func, "template_render");
        duckdb_scalar_function_add_parameter(func, varchar_type);
        duckdb_scalar_function_add_parameter(func, varchar_type);
        duckdb_scalar_function_set_return_type(func, varchar_type);
        duckdb_scalar_function_set_function(func, template_render_func);
        duckdb_register_scalar_function(connection, func);
        duckdb_destroy_scalar_function(&func);
    }

    /* 3-arg: template_render_with_options(template, json_data, options) */
    {
        duckdb_scalar_function func = duckdb_create_scalar_function();
        duckdb_scalar_function_set_name(func, "template_render_with_options");
        duckdb_scalar_function_add_parameter(func, varchar_type);
        duckdb_scalar_function_add_parameter(func, varchar_type);
        duckdb_scalar_function_add_parameter(func, varchar_type);
        duckdb_scalar_function_set_return_type(func, varchar_type);
        duckdb_scalar_function_set_function(func, template_render_opts_func);
        duckdb_register_scalar_function(connection, func);
        duckdb_destroy_scalar_function(&func);
    }

    duckdb_destroy_logical_type(&varchar_type);
}

/* ── Extension entrypoint ────────────────────────────────────────── */

DUCKDB_EXTENSION_ENTRYPOINT(duckdb_connection connection,
                             duckdb_extension_info info,
                             struct duckdb_extension_access *access) {
    register_functions(connection);
    return true;
}
