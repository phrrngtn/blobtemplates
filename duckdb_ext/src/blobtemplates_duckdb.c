/*
 * DuckDB C API extension for Inja template rendering.
 *
 * Registers scalar functions with bt_ prefix (see register_functions).
 * All SQL-visible names use the bt_ prefix to avoid collisions with
 * DuckDB built-ins and other extensions.
 *
 * Uses the simple C extension mechanism (DUCKDB_EXTENSION_ENTRYPOINT).
 */

#define DUCKDB_EXTENSION_NAME blobtemplates
#include "duckdb_extension.h"

#include "blobtemplates.h"

#include <stdlib.h>
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

/* Null-terminate a DuckDB string */
static char *str_dup_z(duckdb_string_t *s) {
    uint32_t len;
    const char *p = str_ptr(s, &len);
    char *z = (char *)malloc(len + 1);
    memcpy(z, p, len);
    z[len] = '\0';
    return z;
}

/* Forward declarations for text_diff functions */
static void text_diff_func(duckdb_function_info, duckdb_data_chunk, duckdb_vector);
static void text_diff_labeled_func(duckdb_function_info, duckdb_data_chunk, duckdb_vector);

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

/* ── Generic 2-arg VARCHAR function (for json_diff, json_patch, etc.) */

typedef char *(*two_arg_cfn)(const char *, const char *);

static void two_arg_varchar_func(duckdb_function_info info,
                                  duckdb_data_chunk input,
                                  duckdb_vector output) {
    two_arg_cfn fn = (two_arg_cfn)duckdb_scalar_function_get_extra_info(info);
    idx_t size = duckdb_data_chunk_get_size(input);
    duckdb_vector vec0 = duckdb_data_chunk_get_vector(input, 0);
    duckdb_vector vec1 = duckdb_data_chunk_get_vector(input, 1);

    duckdb_string_t *data0 = (duckdb_string_t *)duckdb_vector_get_data(vec0);
    duckdb_string_t *data1 = (duckdb_string_t *)duckdb_vector_get_data(vec1);
    uint64_t *val0 = duckdb_vector_get_validity(vec0);
    uint64_t *val1 = duckdb_vector_get_validity(vec1);

    for (idx_t row = 0; row < size; row++) {
        if ((val0 && !duckdb_validity_row_is_valid(val0, row)) ||
            (val1 && !duckdb_validity_row_is_valid(val1, row))) {
            duckdb_vector_ensure_validity_writable(output);
            duckdb_validity_set_row_invalid(duckdb_vector_get_validity(output), row);
            continue;
        }

        uint32_t len0, len1;
        const char *s0 = str_ptr(&data0[row], &len0);
        const char *s1 = str_ptr(&data1[row], &len1);

        /* Core API needs null-terminated strings; DuckDB strings may not be */
        char *z0 = (char *)malloc(len0 + 1);
        char *z1 = (char *)malloc(len1 + 1);
        memcpy(z0, s0, len0); z0[len0] = '\0';
        memcpy(z1, s1, len1); z1[len1] = '\0';

        char *result = fn(z0, z1);
        free(z0);
        free(z1);

        if (result) {
            duckdb_vector_assign_string_element(output, row, result);
            blobtemplates_free(result);
        } else {
            const char *err = blobtemplates_errmsg();
            if (err && err[0]) {
                duckdb_scalar_function_set_error(info, err);
                return;
            }
            /* NULL result (e.g. JMESPath null) */
            duckdb_vector_ensure_validity_writable(output);
            duckdb_validity_set_row_invalid(duckdb_vector_get_validity(output), row);
        }
    }
}

/* ── Generic 1-arg VARCHAR function (flatten, unflatten) ──────────── */

typedef char *(*one_arg_cfn)(const char *);

static void one_arg_varchar_func(duckdb_function_info info,
                                  duckdb_data_chunk input,
                                  duckdb_vector output) {
    one_arg_cfn fn = (one_arg_cfn)duckdb_scalar_function_get_extra_info(info);
    idx_t size = duckdb_data_chunk_get_size(input);
    duckdb_vector vec0 = duckdb_data_chunk_get_vector(input, 0);

    duckdb_string_t *data0 = (duckdb_string_t *)duckdb_vector_get_data(vec0);
    uint64_t *val0 = duckdb_vector_get_validity(vec0);

    for (idx_t row = 0; row < size; row++) {
        if (val0 && !duckdb_validity_row_is_valid(val0, row)) {
            duckdb_vector_ensure_validity_writable(output);
            duckdb_validity_set_row_invalid(duckdb_vector_get_validity(output), row);
            continue;
        }

        uint32_t len0;
        const char *s0 = str_ptr(&data0[row], &len0);

        char *z0 = (char *)malloc(len0 + 1);
        memcpy(z0, s0, len0); z0[len0] = '\0';

        char *result = fn(z0);
        free(z0);

        if (result) {
            duckdb_vector_assign_string_element(output, row, result);
            blobtemplates_free(result);
        } else {
            const char *err = blobtemplates_errmsg();
            if (err && err[0]) {
                duckdb_scalar_function_set_error(info, err);
                return;
            }
            duckdb_vector_ensure_validity_writable(output);
            duckdb_validity_set_row_invalid(duckdb_vector_get_validity(output), row);
        }
    }
}

/* ── Helper: register a 2-arg VARCHAR→VARCHAR function ────────────── */

static void register_two_arg(duckdb_connection conn, const char *name,
                               two_arg_cfn fn, duckdb_logical_type varchar_type) {
    duckdb_scalar_function func = duckdb_create_scalar_function();
    duckdb_scalar_function_set_name(func, name);
    duckdb_scalar_function_add_parameter(func, varchar_type);
    duckdb_scalar_function_add_parameter(func, varchar_type);
    duckdb_scalar_function_set_return_type(func, varchar_type);
    duckdb_scalar_function_set_extra_info(func, (void *)fn, NULL);
    duckdb_scalar_function_set_function(func, two_arg_varchar_func);
    duckdb_register_scalar_function(conn, func);
    duckdb_destroy_scalar_function(&func);
}

/* ── Helper: register a 1-arg VARCHAR→VARCHAR function ────────────── */

static void register_one_arg(duckdb_connection conn, const char *name,
                               one_arg_cfn fn, duckdb_logical_type varchar_type) {
    duckdb_scalar_function func = duckdb_create_scalar_function();
    duckdb_scalar_function_set_name(func, name);
    duckdb_scalar_function_add_parameter(func, varchar_type);
    duckdb_scalar_function_set_return_type(func, varchar_type);
    duckdb_scalar_function_set_extra_info(func, (void *)fn, NULL);
    duckdb_scalar_function_set_function(func, one_arg_varchar_func);
    duckdb_register_scalar_function(conn, func);
    duckdb_destroy_scalar_function(&func);
}

/* ── yaml_to_json: uses length-based API, avoids null-term copy ──── */

static void yaml_to_json_func(duckdb_function_info info,
                                duckdb_data_chunk input,
                                duckdb_vector output) {
    idx_t size = duckdb_data_chunk_get_size(input);
    duckdb_vector vec0 = duckdb_data_chunk_get_vector(input, 0);

    duckdb_string_t *data0 = (duckdb_string_t *)duckdb_vector_get_data(vec0);
    uint64_t *val0 = duckdb_vector_get_validity(vec0);

    for (idx_t row = 0; row < size; row++) {
        if (val0 && !duckdb_validity_row_is_valid(val0, row)) {
            duckdb_vector_ensure_validity_writable(output);
            duckdb_validity_set_row_invalid(duckdb_vector_get_validity(output), row);
            continue;
        }

        uint32_t len0;
        const char *s0 = str_ptr(&data0[row], &len0);

        char *result = blobtemplates_yaml_to_json_n(s0, len0);
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

    /* 2-arg: bt_template_render(template, json_data) */
    {
        duckdb_scalar_function func = duckdb_create_scalar_function();
        duckdb_scalar_function_set_name(func, "bt_template_render");
        duckdb_scalar_function_add_parameter(func, varchar_type);
        duckdb_scalar_function_add_parameter(func, varchar_type);
        duckdb_scalar_function_set_return_type(func, varchar_type);
        duckdb_scalar_function_set_function(func, template_render_func);
        duckdb_register_scalar_function(connection, func);
        duckdb_destroy_scalar_function(&func);
    }

    /* 3-arg: bt_template_render(template, json_data, options) */
    {
        duckdb_scalar_function func = duckdb_create_scalar_function();
        duckdb_scalar_function_set_name(func, "bt_template_render");
        duckdb_scalar_function_add_parameter(func, varchar_type);
        duckdb_scalar_function_add_parameter(func, varchar_type);
        duckdb_scalar_function_add_parameter(func, varchar_type);
        duckdb_scalar_function_set_return_type(func, varchar_type);
        duckdb_scalar_function_set_function(func, template_render_opts_func);
        duckdb_register_scalar_function(connection, func);
        duckdb_destroy_scalar_function(&func);
    }

    /* JMESPath */
    register_two_arg(connection, "bt_jmespath",
                     blobtemplates_jmespath_search, varchar_type);

    /* JSON diff/patch (jsoncons) */
    register_two_arg(connection, "bt_json_from_diff",
                     blobtemplates_json_from_diff, varchar_type);
    register_two_arg(connection, "bt_json_apply_patch",
                     blobtemplates_json_apply_patch, varchar_type);

    /* JSON diff/patch (nlohmann) */
    register_two_arg(connection, "bt_json_diff",
                     blobtemplates_json_diff, varchar_type);
    register_two_arg(connection, "bt_json_patch",
                     blobtemplates_json_patch, varchar_type);

    /* JSON flatten/unflatten */
    register_one_arg(connection, "bt_json_flatten",
                     blobtemplates_json_flatten, varchar_type);
    register_one_arg(connection, "bt_json_unflatten",
                     blobtemplates_json_unflatten, varchar_type);

    /* YAML → JSON (uses length-based API, no null-termination copy) */
    {
        duckdb_scalar_function func = duckdb_create_scalar_function();
        duckdb_scalar_function_set_name(func, "bt_yaml_to_json");
        duckdb_scalar_function_add_parameter(func, varchar_type);
        duckdb_scalar_function_set_return_type(func, varchar_type);
        duckdb_scalar_function_set_function(func, yaml_to_json_func);
        duckdb_register_scalar_function(connection, func);
        duckdb_destroy_scalar_function(&func);
    }

    /* json_nest */
    register_two_arg(connection, "bt_json_nest",
                     blobtemplates_json_nest, varchar_type);

    /* bt_text_diff(old_text, new_text) — 2-arg */
    {
        duckdb_scalar_function func = duckdb_create_scalar_function();
        duckdb_scalar_function_set_name(func, "bt_text_diff");
        duckdb_scalar_function_add_parameter(func, varchar_type);
        duckdb_scalar_function_add_parameter(func, varchar_type);
        duckdb_scalar_function_set_return_type(func, varchar_type);
        duckdb_scalar_function_set_function(func, text_diff_func);
        duckdb_register_scalar_function(connection, func);
        duckdb_destroy_scalar_function(&func);
    }

    /* bt_text_diff(old_text, new_text, label_old, label_new) — 4-arg */
    {
        duckdb_scalar_function func = duckdb_create_scalar_function();
        duckdb_scalar_function_set_name(func, "bt_text_diff");
        duckdb_scalar_function_add_parameter(func, varchar_type);
        duckdb_scalar_function_add_parameter(func, varchar_type);
        duckdb_scalar_function_add_parameter(func, varchar_type);
        duckdb_scalar_function_add_parameter(func, varchar_type);
        duckdb_scalar_function_set_return_type(func, varchar_type);
        duckdb_scalar_function_set_function(func, text_diff_labeled_func);
        duckdb_register_scalar_function(connection, func);
        duckdb_destroy_scalar_function(&func);
    }

    duckdb_destroy_logical_type(&varchar_type);
}

/* ── text_diff DuckDB wrappers ──────────────────────────────────── */

static void text_diff_func(duckdb_function_info info,
                             duckdb_data_chunk input,
                             duckdb_vector output) {
    idx_t size = duckdb_data_chunk_get_size(input);
    duckdb_vector vec0 = duckdb_data_chunk_get_vector(input, 0);
    duckdb_vector vec1 = duckdb_data_chunk_get_vector(input, 1);
    duckdb_string_t *data0 = (duckdb_string_t *)duckdb_vector_get_data(vec0);
    duckdb_string_t *data1 = (duckdb_string_t *)duckdb_vector_get_data(vec1);
    uint64_t *val0 = duckdb_vector_get_validity(vec0);
    uint64_t *val1 = duckdb_vector_get_validity(vec1);

    for (idx_t row = 0; row < size; row++) {
        if ((val0 && !duckdb_validity_row_is_valid(val0, row)) ||
            (val1 && !duckdb_validity_row_is_valid(val1, row))) {
            duckdb_vector_ensure_validity_writable(output);
            duckdb_validity_set_row_invalid(duckdb_vector_get_validity(output), row);
            continue;
        }
        char *old_text = str_dup_z(&data0[row]);
        char *new_text = str_dup_z(&data1[row]);
        char *result = blobtemplates_text_diff(old_text, new_text, NULL, NULL, 3);
        free(old_text); free(new_text);
        if (result) {
            duckdb_vector_assign_string_element(output, row, result);
            blobtemplates_free(result);
        } else {
            duckdb_scalar_function_set_error(info, blobtemplates_errmsg());
            return;
        }
    }
}

static void text_diff_labeled_func(duckdb_function_info info,
                                     duckdb_data_chunk input,
                                     duckdb_vector output) {
    idx_t size = duckdb_data_chunk_get_size(input);
    duckdb_vector vec0 = duckdb_data_chunk_get_vector(input, 0);
    duckdb_vector vec1 = duckdb_data_chunk_get_vector(input, 1);
    duckdb_vector vec2 = duckdb_data_chunk_get_vector(input, 2);
    duckdb_vector vec3 = duckdb_data_chunk_get_vector(input, 3);
    duckdb_string_t *data0 = (duckdb_string_t *)duckdb_vector_get_data(vec0);
    duckdb_string_t *data1 = (duckdb_string_t *)duckdb_vector_get_data(vec1);
    duckdb_string_t *data2 = (duckdb_string_t *)duckdb_vector_get_data(vec2);
    duckdb_string_t *data3 = (duckdb_string_t *)duckdb_vector_get_data(vec3);
    uint64_t *val0 = duckdb_vector_get_validity(vec0);
    uint64_t *val1 = duckdb_vector_get_validity(vec1);

    for (idx_t row = 0; row < size; row++) {
        if ((val0 && !duckdb_validity_row_is_valid(val0, row)) ||
            (val1 && !duckdb_validity_row_is_valid(val1, row))) {
            duckdb_vector_ensure_validity_writable(output);
            duckdb_validity_set_row_invalid(duckdb_vector_get_validity(output), row);
            continue;
        }
        char *old_text  = str_dup_z(&data0[row]);
        char *new_text  = str_dup_z(&data1[row]);
        char *label_old = str_dup_z(&data2[row]);
        char *label_new = str_dup_z(&data3[row]);
        char *result = blobtemplates_text_diff(old_text, new_text, label_old, label_new, 3);
        free(old_text); free(new_text); free(label_old); free(label_new);
        if (result) {
            duckdb_vector_assign_string_element(output, row, result);
            blobtemplates_free(result);
        } else {
            duckdb_scalar_function_set_error(info, blobtemplates_errmsg());
            return;
        }
    }
}

/* ── Extension entrypoint ────────────────────────────────────────── */

DUCKDB_EXTENSION_ENTRYPOINT(duckdb_connection connection,
                             duckdb_extension_info info,
                             struct duckdb_extension_access *access) {
    register_functions(connection);
    return true;
}
