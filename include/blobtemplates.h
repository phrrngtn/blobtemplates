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

/* ── JSON processing functions (via jsoncons + nlohmann) ────────── */

/*
 * JMESPath query: search a JSON document with a JMESPath expression.
 * Returns a malloc'd JSON string (caller must free with blobtemplates_free).
 * Returns NULL if the expression evaluates to JSON null, or on error.
 * Call blobtemplates_errmsg() to distinguish: empty string = null result,
 * non-empty = error.
 */
char *blobtemplates_jmespath_search(const char *json, const char *expression);

/*
 * Compiled JMESPath expression for repeated use.
 * Compile once, search many documents — avoids re-parsing the expression.
 */
typedef struct blobtemplates_jmespath_expr blobtemplates_jmespath_expr;

blobtemplates_jmespath_expr *blobtemplates_jmespath_compile(const char *expression);
char *blobtemplates_jmespath_search_compiled(blobtemplates_jmespath_expr *expr,
                                              const char *json);
void blobtemplates_jmespath_destroy(blobtemplates_jmespath_expr *expr);

/*
 * JSON diff — compute a JSON Patch (RFC 6902) from source to target.
 * Uses jsoncons::jsonpatch::from_diff.
 * Returns a malloc'd JSON Patch array (caller must free with blobtemplates_free).
 */
char *blobtemplates_json_from_diff(const char *source, const char *target);

/*
 * JSON Patch (RFC 6902) — apply a patch to a document.
 * Uses jsoncons::jsonpatch::apply_patch.
 * Returns a malloc'd JSON string (caller must free with blobtemplates_free).
 */
char *blobtemplates_json_apply_patch(const char *json, const char *patch);

/*
 * JSON diff using nlohmann::json::diff (RFC 6902).
 * Returns a malloc'd JSON Patch array (caller must free with blobtemplates_free).
 */
char *blobtemplates_json_diff(const char *source, const char *target);

/*
 * JSON Patch using nlohmann::json::patch (RFC 6902).
 * Returns a malloc'd JSON string (caller must free with blobtemplates_free).
 */
char *blobtemplates_json_patch(const char *source, const char *patch);

/*
 * JSON flatten — convert nested JSON to flat object with JSON Pointer keys.
 * {"a": {"b": 1}} → {"/a/b": 1}
 */
char *blobtemplates_json_flatten(const char *json);

/*
 * JSON unflatten — reverse of flatten.
 * {"/a/b": 1} → {"a": {"b": 1}}
 */
char *blobtemplates_json_unflatten(const char *json);

/* ── YAML processing (via rapidyaml) ─────────────────────────────── */

/*
 * Convert a YAML string to a JSON string.
 *
 * Uses rapidyaml (ryml) for parsing — 10-70x faster than yaml-cpp.
 * Thread-safe: each call parses into its own arena with no shared
 * mutable state, so DuckDB can call this from multiple threads
 * concurrently with full parallelism.
 *
 * Returns a malloc'd JSON string on success (caller must free with
 * blobtemplates_free).
 * Returns NULL on error; call blobtemplates_errmsg() for details.
 */
char *blobtemplates_yaml_to_json(const char *yaml_str);

/*
 * Length-based variant — does not require a null-terminated string.
 * Useful for DuckDB vector string data.
 */
char *blobtemplates_yaml_to_json_n(const char *yaml_str, size_t yaml_len);

#ifdef __cplusplus
}
#endif

#endif
