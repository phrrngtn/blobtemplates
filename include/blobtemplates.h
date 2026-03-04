#ifndef BLOBTEMPLATES_H
#define BLOBTEMPLATES_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Render an Inja/Jinja2-style template against JSON data.
 *
 * Returns a malloc'd string on success (caller must free with blobtemplates_free).
 * Returns NULL on error; call blobtemplates_errmsg() for details.
 */
char *blobtemplates_render(const char *tmpl, const char *json_data);

/*
 * Render with custom delimiter options.
 *
 * options_json is a JSON object with optional keys:
 *   "expression": ["open", "close"]   (default: "{{", "}}")
 *   "statement":  ["open", "close"]   (default: "{%", "%}")
 *   "comment":    ["open", "close"]   (default: "{#", "#}")
 *   "line_statement": "prefix"        (default: "##")
 *
 * Returns a malloc'd string on success (caller must free with blobtemplates_free).
 * Returns NULL on error; call blobtemplates_errmsg() for details.
 */
char *blobtemplates_render_with_options(const char *tmpl, const char *json_data,
                                        const char *options_json);

/*
 * Length-based variants that do not require null-terminated strings.
 * Useful for DuckDB vector string data.
 */
char *blobtemplates_render_n(const char *tmpl, size_t tmpl_len,
                              const char *json_data, size_t json_len);

char *blobtemplates_render_with_options_n(const char *tmpl, size_t tmpl_len,
                                           const char *json_data, size_t json_len,
                                           const char *options_json, size_t opts_len);

/*
 * Free a string returned by blobtemplates_render*.
 */
void blobtemplates_free(char *s);

/* ── Cached rendering (for use with sqlite3_set_auxdata etc.) ────── */

typedef struct blobtemplates_env blobtemplates_env;
typedef struct blobtemplates_tmpl blobtemplates_tmpl;

/*
 * Create/destroy a cached Inja environment.
 * Pass NULL for options_json to use default delimiters.
 */
blobtemplates_env *blobtemplates_env_create(const char *options_json);
void blobtemplates_env_destroy(blobtemplates_env *env);

/*
 * Parse a template string, caching the parsed form.
 * The env may be NULL to use a default environment.
 */
blobtemplates_tmpl *blobtemplates_tmpl_parse(blobtemplates_env *env,
                                              const char *tmpl);
void blobtemplates_tmpl_destroy(blobtemplates_tmpl *t);

/*
 * Render a cached template against JSON data.
 * Returns a malloc'd string (caller must free with blobtemplates_free).
 * Returns NULL on error; call blobtemplates_errmsg() for details.
 */
char *blobtemplates_render_cached(blobtemplates_env *env,
                                   blobtemplates_tmpl *t,
                                   const char *json_data);

/*
 * Return the last error message (thread-local).
 * Returns "" if no error has occurred.
 */
const char *blobtemplates_errmsg(void);

/*
 * Clear the process-wide template parse cache.
 * Parsed templates are cached automatically so repeated calls with
 * the same template string skip re-parsing.  Call this if you need
 * to reclaim memory.
 */
void blobtemplates_cache_clear(void);

#ifdef __cplusplus
}
#endif

#endif
