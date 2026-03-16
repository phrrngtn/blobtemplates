/*
 * SQLite loadable extension for Inja template rendering.
 *
 * Registers:
 *   template_render(template, json_data)           -> TEXT
 *   template_render(template, json_data, options)   -> TEXT
 *
 * Uses sqlite3_set_auxdata/sqlite3_get_auxdata to cache:
 *   - The parsed inja::Template (keyed on arg 0 — the template string)
 *   - The inja::Environment    (keyed on arg 2 — the options JSON)
 * so that repeated calls with the same template/options (the common case
 * in SELECT over a table) avoid re-parsing.
 */

#include <sqlite3ext.h>
SQLITE_EXTENSION_INIT1

#include "blobtemplates.h"

#include <stdlib.h>
#include <string.h>

/* ── auxdata destructor callbacks ─────────────────────────────────── */

static void free_cached_tmpl(void *p) {
    blobtemplates_tmpl_destroy((blobtemplates_tmpl *)p);
}

static void free_cached_env(void *p) {
    blobtemplates_env_destroy((blobtemplates_env *)p);
}

static void free_cached_jmespath(void *p) {
    blobtemplates_jmespath_destroy((blobtemplates_jmespath_expr *)p);
}

/* ── main scalar function ─────────────────────────────────────────── */

static void inja_func(sqlite3_context *ctx, int argc, sqlite3_value **argv) {
    const char *tmpl_str;
    const char *json_data;
    blobtemplates_env *env = NULL;
    blobtemplates_tmpl *tmpl = NULL;
    char *result;

    if (sqlite3_value_type(argv[0]) == SQLITE_NULL ||
        sqlite3_value_type(argv[1]) == SQLITE_NULL) {
        sqlite3_result_null(ctx);
        return;
    }

    tmpl_str  = (const char *)sqlite3_value_text(argv[0]);
    json_data = (const char *)sqlite3_value_text(argv[1]);

    /* Try to reuse cached environment (arg 2, options) */
    if (argc == 3 && sqlite3_value_type(argv[2]) != SQLITE_NULL) {
        const char *options = (const char *)sqlite3_value_text(argv[2]);
        env = (blobtemplates_env *)sqlite3_get_auxdata(ctx, 2);
        if (!env) {
            env = blobtemplates_env_create(options);
            if (!env) {
                sqlite3_result_error(ctx, blobtemplates_errmsg(), -1);
                return;
            }
            sqlite3_set_auxdata(ctx, 2, env, free_cached_env);
            /* Re-fetch: sqlite3_set_auxdata may have freed the old one */
            env = (blobtemplates_env *)sqlite3_get_auxdata(ctx, 2);
        }
    }

    /* Try to reuse cached parsed template (arg 0) */
    tmpl = (blobtemplates_tmpl *)sqlite3_get_auxdata(ctx, 0);
    if (!tmpl) {
        tmpl = blobtemplates_tmpl_parse(env, tmpl_str);
        if (!tmpl) {
            sqlite3_result_error(ctx, blobtemplates_errmsg(), -1);
            return;
        }
        sqlite3_set_auxdata(ctx, 0, tmpl, free_cached_tmpl);
        tmpl = (blobtemplates_tmpl *)sqlite3_get_auxdata(ctx, 0);
    }

    /* Render using cached objects */
    if (tmpl) {
        result = blobtemplates_render_cached(env, tmpl, json_data);
    } else if (argc == 3 && sqlite3_value_type(argv[2]) != SQLITE_NULL) {
        /* Fallback if auxdata not retained */
        const char *options = (const char *)sqlite3_value_text(argv[2]);
        result = blobtemplates_render_with_options(tmpl_str, json_data, options);
    } else {
        result = blobtemplates_render(tmpl_str, json_data);
    }

    if (result) {
        sqlite3_result_text(ctx, result, -1, SQLITE_TRANSIENT);
        blobtemplates_free(result);
    } else {
        sqlite3_result_error(ctx, blobtemplates_errmsg(), -1);
    }
}

/* ── jmespath_search(json, expression) ────────────────────────────── */

static void jmespath_search_func(sqlite3_context *ctx, int argc,
                                  sqlite3_value **argv) {
    if (sqlite3_value_type(argv[0]) == SQLITE_NULL ||
        sqlite3_value_type(argv[1]) == SQLITE_NULL) {
        sqlite3_result_null(ctx);
        return;
    }

    const char *json = (const char *)sqlite3_value_text(argv[0]);
    const char *expression = (const char *)sqlite3_value_text(argv[1]);

    /* Try to reuse compiled expression (arg 1) */
    blobtemplates_jmespath_expr *expr =
        (blobtemplates_jmespath_expr *)sqlite3_get_auxdata(ctx, 1);
    char *result;

    if (expr) {
        result = blobtemplates_jmespath_search_compiled(expr, json);
    } else {
        expr = blobtemplates_jmespath_compile(expression);
        if (!expr) {
            sqlite3_result_error(ctx, blobtemplates_errmsg(), -1);
            return;
        }
        sqlite3_set_auxdata(ctx, 1, expr, free_cached_jmespath);
        /* Re-fetch in case auxdata was not retained */
        expr = (blobtemplates_jmespath_expr *)sqlite3_get_auxdata(ctx, 1);
        if (expr) {
            result = blobtemplates_jmespath_search_compiled(expr, json);
        } else {
            result = blobtemplates_jmespath_search(json, expression);
        }
    }

    if (result) {
        sqlite3_result_text(ctx, result, -1, SQLITE_TRANSIENT);
        blobtemplates_free(result);
    } else {
        const char *err = blobtemplates_errmsg();
        if (err && err[0]) {
            sqlite3_result_error(ctx, err, -1);
        } else {
            sqlite3_result_null(ctx);  /* JMESPath null result */
        }
    }
}

/* ── Two-arg JSON functions (source, target/expression/patch) ──────── */

typedef char *(*two_arg_json_fn)(const char *, const char *);

static void two_arg_func(sqlite3_context *ctx, int argc,
                           sqlite3_value **argv) {
    two_arg_json_fn fn = (two_arg_json_fn)sqlite3_user_data(ctx);

    if (sqlite3_value_type(argv[0]) == SQLITE_NULL ||
        sqlite3_value_type(argv[1]) == SQLITE_NULL) {
        sqlite3_result_null(ctx);
        return;
    }

    const char *a = (const char *)sqlite3_value_text(argv[0]);
    const char *b = (const char *)sqlite3_value_text(argv[1]);

    char *result = fn(a, b);
    if (result) {
        sqlite3_result_text(ctx, result, -1, SQLITE_TRANSIENT);
        blobtemplates_free(result);
    } else {
        const char *err = blobtemplates_errmsg();
        if (err && err[0]) {
            sqlite3_result_error(ctx, err, -1);
        } else {
            sqlite3_result_null(ctx);
        }
    }
}

/* ── Single-arg JSON functions (flatten, unflatten) ────────────────── */

typedef char *(*one_arg_json_fn)(const char *);

static void one_arg_func(sqlite3_context *ctx, int argc,
                           sqlite3_value **argv) {
    one_arg_json_fn fn = (one_arg_json_fn)sqlite3_user_data(ctx);

    if (sqlite3_value_type(argv[0]) == SQLITE_NULL) {
        sqlite3_result_null(ctx);
        return;
    }

    const char *a = (const char *)sqlite3_value_text(argv[0]);

    char *result = fn(a);
    if (result) {
        sqlite3_result_text(ctx, result, -1, SQLITE_TRANSIENT);
        blobtemplates_free(result);
    } else {
        const char *err = blobtemplates_errmsg();
        if (err && err[0]) {
            sqlite3_result_error(ctx, err, -1);
        } else {
            sqlite3_result_null(ctx);
        }
    }
}

/* ── text_diff (2 or 4 args) ─────────────────────────────────────── */

static void text_diff_func(sqlite3_context *ctx, int argc, sqlite3_value **argv) {
    if (argc < 2) {
        sqlite3_result_error(ctx, "text_diff requires at least 2 arguments", -1);
        return;
    }
    if (sqlite3_value_type(argv[0]) == SQLITE_NULL ||
        sqlite3_value_type(argv[1]) == SQLITE_NULL) {
        sqlite3_result_null(ctx);
        return;
    }

    const char *old_text = (const char *)sqlite3_value_text(argv[0]);
    const char *new_text = (const char *)sqlite3_value_text(argv[1]);
    const char *label_old = (argc >= 3 && sqlite3_value_type(argv[2]) != SQLITE_NULL)
                             ? (const char *)sqlite3_value_text(argv[2]) : NULL;
    const char *label_new = (argc >= 4 && sqlite3_value_type(argv[3]) != SQLITE_NULL)
                             ? (const char *)sqlite3_value_text(argv[3]) : NULL;

    char *result = blobtemplates_text_diff(old_text, new_text, label_old, label_new, 3);
    if (result) {
        sqlite3_result_text(ctx, result, -1, SQLITE_TRANSIENT);
        blobtemplates_free(result);
    } else {
        sqlite3_result_error(ctx, blobtemplates_errmsg(), -1);
    }
}

/* ── Extension init ──────────────────────────────────────────────── */

#ifdef _WIN32
__declspec(dllexport)
#endif
int sqlite3_blobtemplates_init(sqlite3 *db, char **pzErrMsg,
                                const sqlite3_api_routines *pApi) {
    int rc;
    SQLITE_EXTENSION_INIT2(pApi);

    rc = sqlite3_create_function(db, "template_render", 2,
                                  SQLITE_UTF8 | SQLITE_DETERMINISTIC,
                                  NULL, inja_func, NULL, NULL);
    if (rc != SQLITE_OK) return rc;

    rc = sqlite3_create_function(db, "template_render", 3,
                                  SQLITE_UTF8 | SQLITE_DETERMINISTIC,
                                  NULL, inja_func, NULL, NULL);
    if (rc != SQLITE_OK) return rc;

    /* JMESPath */
    rc = sqlite3_create_function(db, "jmespath_search", 2,
                                  SQLITE_UTF8 | SQLITE_DETERMINISTIC,
                                  NULL, jmespath_search_func, NULL, NULL);
    if (rc != SQLITE_OK) return rc;

    /* JSON diff/patch (jsoncons) */
    rc = sqlite3_create_function(db, "json_from_diff", 2,
                                  SQLITE_UTF8 | SQLITE_DETERMINISTIC,
                                  (void *)blobtemplates_json_from_diff,
                                  two_arg_func, NULL, NULL);
    if (rc != SQLITE_OK) return rc;

    rc = sqlite3_create_function(db, "json_apply_patch", 2,
                                  SQLITE_UTF8 | SQLITE_DETERMINISTIC,
                                  (void *)blobtemplates_json_apply_patch,
                                  two_arg_func, NULL, NULL);
    if (rc != SQLITE_OK) return rc;

    /* JSON diff/patch (nlohmann) */
    rc = sqlite3_create_function(db, "json_diff", 2,
                                  SQLITE_UTF8 | SQLITE_DETERMINISTIC,
                                  (void *)blobtemplates_json_diff,
                                  two_arg_func, NULL, NULL);
    if (rc != SQLITE_OK) return rc;

    rc = sqlite3_create_function(db, "json_patch", 2,
                                  SQLITE_UTF8 | SQLITE_DETERMINISTIC,
                                  (void *)blobtemplates_json_patch,
                                  two_arg_func, NULL, NULL);
    if (rc != SQLITE_OK) return rc;

    /* JSON flatten/unflatten */
    rc = sqlite3_create_function(db, "json_flatten", 1,
                                  SQLITE_UTF8 | SQLITE_DETERMINISTIC,
                                  (void *)blobtemplates_json_flatten,
                                  one_arg_func, NULL, NULL);
    if (rc != SQLITE_OK) return rc;

    rc = sqlite3_create_function(db, "json_unflatten", 1,
                                  SQLITE_UTF8 | SQLITE_DETERMINISTIC,
                                  (void *)blobtemplates_json_unflatten,
                                  one_arg_func, NULL, NULL);
    if (rc != SQLITE_OK) return rc;

    /* json_nest */
    rc = sqlite3_create_function(db, "json_nest", 2,
                                  SQLITE_UTF8 | SQLITE_DETERMINISTIC,
                                  (void *)blobtemplates_json_nest,
                                  two_arg_func, NULL, NULL);
    if (rc != SQLITE_OK) return rc;

    /* text_diff (variadic: 2 or 4 args) */
    rc = sqlite3_create_function(db, "text_diff", -1,
                                  SQLITE_UTF8 | SQLITE_DETERMINISTIC,
                                  NULL, text_diff_func, NULL, NULL);
    if (rc != SQLITE_OK) return rc;

    /* YAML → JSON */
    rc = sqlite3_create_function(db, "yaml_to_json", 1,
                                  SQLITE_UTF8 | SQLITE_DETERMINISTIC,
                                  (void *)blobtemplates_yaml_to_json,
                                  one_arg_func, NULL, NULL);
    return rc;
}
