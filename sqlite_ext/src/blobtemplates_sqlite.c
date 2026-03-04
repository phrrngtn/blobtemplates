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
    return rc;
}
