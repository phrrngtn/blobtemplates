#include "blobtemplates.h"

#include <inja/inja.hpp>
#include <nlohmann/json.hpp>
#include <jsoncons/json.hpp>
#include <jsoncons_ext/jmespath/jmespath.hpp>
#include <jsoncons_ext/jsonpatch/jsonpatch.hpp>
#include <ryml.hpp>
#include <ryml_std.hpp>

#include <cstring>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>

static thread_local std::string g_errmsg;

static char *strdup_result(const std::string &s) {
    char *out = (char *)malloc(s.size() + 1);
    if (out) memcpy(out, s.data(), s.size() + 1);
    return out;
}

static void configure_env(inja::Environment &env, const nlohmann::json &opts) {
    if (opts.contains("expression") && opts["expression"].is_array() &&
        opts["expression"].size() == 2) {
        env.set_expression(opts["expression"][0].get<std::string>(),
                           opts["expression"][1].get<std::string>());
    }
    if (opts.contains("statement") && opts["statement"].is_array() &&
        opts["statement"].size() == 2) {
        env.set_statement(opts["statement"][0].get<std::string>(),
                          opts["statement"][1].get<std::string>());
    }
    if (opts.contains("comment") && opts["comment"].is_array() &&
        opts["comment"].size() == 2) {
        env.set_comment(opts["comment"][0].get<std::string>(),
                        opts["comment"][1].get<std::string>());
    }
    if (opts.contains("line_statement") && opts["line_statement"].is_string()) {
        env.set_line_statement(opts["line_statement"].get<std::string>());
    }
}

/* ── Template cache ───────────────────────────────────────────────
 *
 * Process-wide LRU cache protected by a shared_mutex.
 *
 * Keys store the actual template + options strings (not just hashes)
 * so that hash collisions are handled correctly.
 *
 * Each CacheEntry holds a parsed inja::Template.  We do NOT cache
 * the inja::Environment because Environment::render() is not
 * documented as thread-safe.  Instead, each render call creates a
 * lightweight Environment on the stack and uses render_to() which
 * constructs a temporary Renderer from const refs to the template.
 * The only shared state is the immutable parsed Template.
 *
 * The cache is capped at CACHE_MAX_SIZE entries with LRU eviction.
 */

static constexpr size_t CACHE_MAX_SIZE = 1024;

struct CacheKey {
    std::string tmpl;
    std::string opts;   /* empty for default delimiters */
    std::size_t hash;   /* precomputed for bucket selection */

    CacheKey(std::string_view t, std::string_view o)
        : tmpl(t), opts(o) {
        std::hash<std::string_view> h;
        hash = h(t) ^ (h(o) * 0x9e3779b97f4a7c15ULL + 0x9e3779b9 +
                        (h(t) << 6) + (h(t) >> 2));
    }

    bool operator==(const CacheKey &other) const {
        return tmpl == other.tmpl && opts == other.opts;
    }
};

struct CacheKeyHash {
    std::size_t operator()(const CacheKey &k) const { return k.hash; }
};

/* LRU list: front = most recently used */
using LruList = std::list<CacheKey>;

struct CacheEntry {
    std::shared_ptr<inja::Template> tmpl;
    LruList::iterator lru_it;
};

static std::shared_mutex g_cache_mu;
static std::unordered_map<CacheKey, CacheEntry, CacheKeyHash> g_cache;
static LruList g_lru;

/*
 * Look up or parse a template.  Returns a shared_ptr so the caller
 * can hold a reference after releasing the lock.
 */
static std::shared_ptr<inja::Template> cache_lookup(std::string_view tmpl_str,
                                                     std::string_view opts_str) {
    CacheKey probe(tmpl_str, opts_str);

    /* Fast path: shared (read) lock */
    {
        std::shared_lock lock(g_cache_mu);
        auto it = g_cache.find(probe);
        if (it != g_cache.end()) {
            /* Note: we skip LRU promotion under read lock to avoid
               contention.  Stale ordering is acceptable — it only
               affects which entry gets evicted, not correctness. */
            return it->second.tmpl;
        }
    }

    /* Slow path: unique (write) lock — parse the template */
    std::unique_lock lock(g_cache_mu);

    /* Double-check after acquiring write lock */
    auto it = g_cache.find(probe);
    if (it != g_cache.end()) {
        /* Promote to front of LRU */
        g_lru.splice(g_lru.begin(), g_lru, it->second.lru_it);
        return it->second.tmpl;
    }

    /* Parse */
    inja::Environment env;
    if (!opts_str.empty()) {
        nlohmann::json opts = nlohmann::json::parse(opts_str);
        configure_env(env, opts);
    }
    auto t = std::make_shared<inja::Template>(env.parse(tmpl_str));

    /* Evict LRU entries if at capacity */
    while (g_cache.size() >= CACHE_MAX_SIZE) {
        auto &victim = g_lru.back();
        g_cache.erase(victim);
        g_lru.pop_back();
    }

    /* Insert at front of LRU */
    g_lru.push_front(probe);
    g_cache.emplace(std::move(probe), CacheEntry{t, g_lru.begin()});
    return t;
}

/* ── Render helpers (create a stack-local Environment per call) ──── */

static char *do_render(std::string_view tmpl_sv, std::string_view opts_sv,
                        std::string_view json_sv) {
    try {
        g_errmsg.clear();
        nlohmann::json data = nlohmann::json::parse(json_sv);
        auto t = cache_lookup(tmpl_sv, opts_sv);

        /* Build a throwaway Environment — cheap, and avoids sharing
           mutable state across threads. */
        inja::Environment env;
        if (!opts_sv.empty()) {
            nlohmann::json opts = nlohmann::json::parse(opts_sv);
            configure_env(env, opts);
        }
        std::string result = env.render(*t, data);
        return strdup_result(result);
    } catch (const inja::InjaError &e) {
        g_errmsg = e.what();
        return nullptr;
    } catch (const nlohmann::json::exception &e) {
        g_errmsg = e.what();
        return nullptr;
    }
}

/* ── Opaque types for explicit caching (used by SQLite auxdata) ─── */

struct blobtemplates_env {
    inja::Environment env;
};

struct blobtemplates_tmpl {
    inja::Template tmpl;
    blobtemplates_tmpl(inja::Template t) : tmpl(std::move(t)) {}
};

/* ── C API ────────────────────────────────────────────────────────── */

extern "C" {

char *blobtemplates_render(const char *tmpl, const char *json_data) {
    return do_render(tmpl, std::string_view{}, json_data);
}

char *blobtemplates_render_with_options(const char *tmpl, const char *json_data,
                                         const char *options_json) {
    return do_render(tmpl, options_json, json_data);
}

char *blobtemplates_render_n(const char *tmpl, size_t tmpl_len,
                              const char *json_data, size_t json_len) {
    return do_render(std::string_view(tmpl, tmpl_len),
                     std::string_view{},
                     std::string_view(json_data, json_len));
}

char *blobtemplates_render_with_options_n(const char *tmpl, size_t tmpl_len,
                                           const char *json_data, size_t json_len,
                                           const char *options_json, size_t opts_len) {
    return do_render(std::string_view(tmpl, tmpl_len),
                     std::string_view(options_json, opts_len),
                     std::string_view(json_data, json_len));
}

void blobtemplates_free(char *s) {
    free(s);
}

const char *blobtemplates_errmsg(void) {
    return g_errmsg.c_str();
}

void blobtemplates_cache_clear(void) {
    std::unique_lock lock(g_cache_mu);
    g_cache.clear();
    g_lru.clear();
}

/* ── Explicit caching API (for SQLite auxdata) ────────────────────── */

blobtemplates_env *blobtemplates_env_create(const char *options_json) {
    try {
        g_errmsg.clear();
        auto *e = new blobtemplates_env();
        if (options_json) {
            nlohmann::json opts = nlohmann::json::parse(options_json);
            configure_env(e->env, opts);
        }
        return e;
    } catch (const nlohmann::json::exception &ex) {
        g_errmsg = ex.what();
        return nullptr;
    }
}

void blobtemplates_env_destroy(blobtemplates_env *env) {
    delete env;
}

blobtemplates_tmpl *blobtemplates_tmpl_parse(blobtemplates_env *env,
                                              const char *tmpl) {
    try {
        g_errmsg.clear();
        inja::Environment *e;
        inja::Environment default_env;
        if (env) {
            e = &env->env;
        } else {
            e = &default_env;
        }
        inja::Template t = e->parse(tmpl);
        return new blobtemplates_tmpl(std::move(t));
    } catch (const inja::InjaError &ex) {
        g_errmsg = ex.what();
        return nullptr;
    }
}

void blobtemplates_tmpl_destroy(blobtemplates_tmpl *t) {
    delete t;
}

char *blobtemplates_render_cached(blobtemplates_env *env,
                                   blobtemplates_tmpl *t,
                                   const char *json_data) {
    try {
        g_errmsg.clear();
        nlohmann::json data = nlohmann::json::parse(json_data);
        inja::Environment *e;
        inja::Environment default_env;
        if (env) {
            e = &env->env;
        } else {
            e = &default_env;
        }
        std::string result = e->render(t->tmpl, data);
        return strdup_result(result);
    } catch (const inja::InjaError &ex) {
        g_errmsg = ex.what();
        return nullptr;
    } catch (const nlohmann::json::exception &ex) {
        g_errmsg = ex.what();
        return nullptr;
    }
}

/* ── Custom JMESPath functions ─────────────────────────────────
 *
 * Registered into every JMESPath evaluation so they're available
 * from DuckDB, SQLite, and Python without any per-call setup.
 *
 *   zip_arrays(obj)   — {a:[1,2], b:[3,4]} → [{a:1,b:3}, {a:2,b:4}]
 *   to_entries(obj)    — {k1:v1, k2:v2}     → [{key:k1,value:v1}, ...]
 *
 * Thread-safe: the custom_functions instance is stateless and
 * constructed per-call (cheap — just a vector of function pointers).
 */

using JsonT = jsoncons::json;
using JmesParam = jsoncons::jmespath::parameter<JsonT>;
using JmesCtx = jsoncons::jmespath::eval_context<JsonT>;

class blob_jmespath_functions
    : public jsoncons::jmespath::custom_functions<JsonT> {
public:
    blob_jmespath_functions() {
        /* zip_arrays(obj) → array of objects
         *
         * Takes an object whose values are all arrays of the same length.
         * Transposes from columnar to row-oriented:
         *   {time:["a","b"], temp:[1,2]} → [{time:"a",temp:1}, {time:"b",temp:2}]
         *
         * Designed for APIs that return parallel arrays (Open-Meteo, charting
         * endpoints, etc.) to normalize them to the same shape as APIs that
         * return array-of-objects.
         */
        this->register_function("zip_arrays", 1,
            [](jsoncons::span<const JmesParam> params,
               JmesCtx &context, std::error_code &ec) -> JsonT
            {
                if (!params[0].is_value() || !params[0].value().is_object()) {
                    ec = jsoncons::jmespath::jmespath_errc::invalid_argument;
                    return context.null_value();
                }
                const auto &obj = params[0].value();

                /* Find the array length (all must match) */
                std::size_t len = 0;
                bool first = true;
                for (const auto &member : obj.object_range()) {
                    if (!member.value().is_array()) {
                        ec = jsoncons::jmespath::jmespath_errc::invalid_argument;
                        return context.null_value();
                    }
                    std::size_t n = member.value().size();
                    if (first) { len = n; first = false; }
                    else if (n != len) {
                        /* Arrays not same length — use shortest */
                        if (n < len) len = n;
                    }
                }

                JsonT result = JsonT(jsoncons::json_array_arg);
                for (std::size_t i = 0; i < len; ++i) {
                    JsonT row = JsonT(jsoncons::json_object_arg);
                    for (const auto &member : obj.object_range()) {
                        row.insert_or_assign(member.key(), member.value().at(i));
                    }
                    result.push_back(std::move(row));
                }
                return result;
            }
        );

        /* unzip_arrays(arr) → {k1: [...], k2: [...]}
         *
         * Inverse of zip_arrays. Takes an array of objects and transposes
         * to an object of parallel arrays (columnar layout):
         *   [{a:1, b:"x"}, {a:2, b:"y"}] → {a:[1,2], b:["x","y"]}
         *
         * Keys are taken from the first element. Missing keys in
         * subsequent elements produce null values in the output arrays.
         */
        this->register_function("unzip_arrays", 1,
            [](jsoncons::span<const JmesParam> params,
               JmesCtx &context, std::error_code &ec) -> JsonT
            {
                if (!params[0].is_value() || !params[0].value().is_array()) {
                    ec = jsoncons::jmespath::jmespath_errc::invalid_argument;
                    return context.null_value();
                }
                const auto &arr = params[0].value();
                if (arr.size() == 0) {
                    return JsonT(jsoncons::json_object_arg);
                }

                /* Collect all keys from the first element */
                if (!arr[0].is_object()) {
                    ec = jsoncons::jmespath::jmespath_errc::invalid_argument;
                    return context.null_value();
                }

                std::vector<std::string> keys;
                for (const auto &member : arr[0].object_range()) {
                    keys.push_back(std::string(member.key()));
                }

                /* Build parallel arrays */
                JsonT result(jsoncons::json_object_arg);
                for (const auto &key : keys) {
                    JsonT col(jsoncons::json_array_arg);
                    for (std::size_t i = 0; i < arr.size(); ++i) {
                        if (arr[i].is_object() && arr[i].contains(key)) {
                            col.push_back(arr[i].at(key));
                        } else {
                            col.push_back(JsonT::null());
                        }
                    }
                    result.insert_or_assign(key, std::move(col));
                }
                return result;
            }
        );

        /* to_entries(obj) → [{key: k, value: v}, ...]
         *
         * Converts an object to an array of key-value pair objects.
         * Useful for iterating over response objects with dynamic keys
         * (e.g., ID-keyed maps common in API responses).
         */
        this->register_function("to_entries", 1,
            [](jsoncons::span<const JmesParam> params,
               JmesCtx &context, std::error_code &ec) -> JsonT
            {
                if (!params[0].is_value() || !params[0].value().is_object()) {
                    ec = jsoncons::jmespath::jmespath_errc::invalid_argument;
                    return context.null_value();
                }
                const auto &obj = params[0].value();

                JsonT result = JsonT(jsoncons::json_array_arg);
                for (const auto &member : obj.object_range()) {
                    JsonT entry = JsonT(jsoncons::json_object_arg);
                    entry.insert_or_assign("key", JsonT(member.key()));
                    entry.insert_or_assign("value", member.value());
                    result.push_back(std::move(entry));
                }
                return result;
            }
        );

        /* from_entries(arr) → {k1: v1, k2: v2, ...}
         *
         * Inverse of to_entries. Takes an array of {key, value} objects
         * and reconstructs an object:
         *   [{key:"a", value:1}, {key:"b", value:2}] → {a:1, b:2}
         *
         * Elements missing "key" or "value" fields are skipped.
         */
        this->register_function("from_entries", 1,
            [](jsoncons::span<const JmesParam> params,
               JmesCtx &context, std::error_code &ec) -> JsonT
            {
                if (!params[0].is_value() || !params[0].value().is_array()) {
                    ec = jsoncons::jmespath::jmespath_errc::invalid_argument;
                    return context.null_value();
                }
                const auto &arr = params[0].value();

                JsonT result(jsoncons::json_object_arg);
                for (std::size_t i = 0; i < arr.size(); ++i) {
                    if (!arr[i].is_object()) continue;
                    if (!arr[i].contains("key") || !arr[i].contains("value")) continue;

                    auto &key_val = arr[i].at("key");
                    std::string key;
                    if (key_val.is_string()) {
                        key = key_val.template as<std::string>();
                    } else {
                        /* Coerce non-string keys to their JSON representation */
                        key_val.dump(key);
                    }
                    result.insert_or_assign(key, arr[i].at("value"));
                }
                return result;
            }
        );
    }
};

/* Singleton accessor — cheap to construct (just function pointers),
   but avoid doing it on every call. */
static const blob_jmespath_functions &get_custom_functions() {
    static const blob_jmespath_functions instance;
    return instance;
}

/* ── JSON processing functions ─────────────────────────────────── */

char *blobtemplates_jmespath_search(const char *json, const char *expression) {
    try {
        g_errmsg.clear();
        auto doc = jsoncons::json::parse(json);
        /* Use make_expression so custom functions are available */
        auto expr = jsoncons::jmespath::make_expression<jsoncons::json>(
            expression, get_custom_functions());
        auto result = expr.evaluate(doc);
        if (result.is_null()) {
            return nullptr;  /* g_errmsg is empty → null result, not error */
        }
        std::string s;
        result.dump(s);
        return strdup_result(s);
    } catch (const std::exception &e) {
        g_errmsg = e.what();
        return nullptr;
    }
}

/* ── Compiled JMESPath expression ─────────────────────────────── */

struct blobtemplates_jmespath_expr {
    jsoncons::jmespath::jmespath_expression<jsoncons::json> expr;
    blobtemplates_jmespath_expr(
        jsoncons::jmespath::jmespath_expression<jsoncons::json> e)
        : expr(std::move(e)) {}
};

blobtemplates_jmespath_expr *blobtemplates_jmespath_compile(const char *expression) {
    try {
        g_errmsg.clear();
        /* Custom functions baked into the compiled expression */
        auto expr = jsoncons::jmespath::make_expression<jsoncons::json>(
            expression, get_custom_functions());
        return new blobtemplates_jmespath_expr(std::move(expr));
    } catch (const std::exception &e) {
        g_errmsg = e.what();
        return nullptr;
    }
}

char *blobtemplates_jmespath_search_compiled(blobtemplates_jmespath_expr *expr,
                                              const char *json) {
    try {
        g_errmsg.clear();
        auto doc = jsoncons::json::parse(json);
        auto result = expr->expr.evaluate(doc);
        if (result.is_null()) {
            return nullptr;
        }
        std::string s;
        result.dump(s);
        return strdup_result(s);
    } catch (const std::exception &e) {
        g_errmsg = e.what();
        return nullptr;
    }
}

void blobtemplates_jmespath_destroy(blobtemplates_jmespath_expr *expr) {
    delete expr;
}

/* ── JSON diff/patch (jsoncons) ───────────────────────────────── */

char *blobtemplates_json_from_diff(const char *source, const char *target) {
    try {
        g_errmsg.clear();
        auto src = jsoncons::json::parse(source ? source : "null");
        auto tgt = jsoncons::json::parse(target ? target : "null");
        auto patch = jsoncons::jsonpatch::from_diff(src, tgt);
        std::string s;
        patch.dump(s);
        return strdup_result(s);
    } catch (const std::exception &e) {
        g_errmsg = e.what();
        return nullptr;
    }
}

char *blobtemplates_json_apply_patch(const char *json, const char *patch) {
    try {
        g_errmsg.clear();
        auto doc = jsoncons::json::parse(json ? json : "null");
        auto p = jsoncons::json::parse(patch ? patch : "[]");
        jsoncons::jsonpatch::apply_patch(doc, p);
        std::string s;
        doc.dump(s);
        return strdup_result(s);
    } catch (const std::exception &e) {
        g_errmsg = e.what();
        return nullptr;
    }
}

/* ── JSON diff/patch (nlohmann) ───────────────────────────────── */

char *blobtemplates_json_diff(const char *source, const char *target) {
    try {
        g_errmsg.clear();
        auto src = nlohmann::json::parse(source ? source : "null");
        auto tgt = nlohmann::json::parse(target ? target : "null");
        auto patch = nlohmann::json::diff(src, tgt);
        return strdup_result(patch.dump());
    } catch (const std::exception &e) {
        g_errmsg = e.what();
        return nullptr;
    }
}

char *blobtemplates_json_patch(const char *source, const char *patch) {
    try {
        g_errmsg.clear();
        auto src = nlohmann::json::parse(source ? source : "null");
        auto p = nlohmann::json::parse(patch ? patch : "[]");
        auto result = src.patch(p);
        return strdup_result(result.dump());
    } catch (const std::exception &e) {
        g_errmsg = e.what();
        return nullptr;
    }
}

/* ── JSON flatten/unflatten (nlohmann) ────────────────────────── */

char *blobtemplates_json_flatten(const char *json) {
    try {
        g_errmsg.clear();
        auto doc = nlohmann::json::parse(json);
        return strdup_result(doc.flatten().dump());
    } catch (const std::exception &e) {
        g_errmsg = e.what();
        return nullptr;
    }
}

char *blobtemplates_json_unflatten(const char *json) {
    try {
        g_errmsg.clear();
        auto doc = nlohmann::json::parse(json);
        return strdup_result(doc.unflatten().dump());
    } catch (const std::exception &e) {
        g_errmsg = e.what();
        return nullptr;
    }
}

/* ── YAML → JSON (rapidyaml) ──────────────────────────────────── */

/*
 * Escape JSON control characters that ryml's emitter passes through
 * literally. JSON requires all U+0000..U+001F to be escaped as \uXXXX
 * (except \n, \r, \t, \b, \f which have short forms but should also be
 * escaped when they appear raw).
 *
 * We scan the emitted JSON and only fix characters inside string values
 * (between unescaped quotes) to avoid breaking JSON structure.
 */
static std::string escape_json_control_chars(const std::string &json) {
    std::string out;
    out.reserve(json.size() + json.size() / 64);  /* small margin */
    bool in_string = false;
    bool escaped = false;

    for (size_t i = 0; i < json.size(); ++i) {
        unsigned char ch = static_cast<unsigned char>(json[i]);

        if (escaped) {
            out.push_back(ch);
            escaped = false;
            continue;
        }

        if (ch == '\\' && in_string) {
            out.push_back(ch);
            escaped = true;
            continue;
        }

        if (ch == '"') {
            in_string = !in_string;
            out.push_back(ch);
            continue;
        }

        if (in_string && ch < 0x20) {
            /* Escape control character as \uXXXX */
            char buf[8];
            snprintf(buf, sizeof(buf), "\\u%04x", ch);
            out.append(buf);
            continue;
        }

        out.push_back(ch);
    }
    return out;
}

char *blobtemplates_yaml_to_json(const char *yaml_str) {
    return blobtemplates_yaml_to_json_n(yaml_str, strlen(yaml_str));
}

char *blobtemplates_yaml_to_json_n(const char *yaml_str, size_t yaml_len) {
    try {
        g_errmsg.clear();
        /* parse_in_arena copies the input into a tree-owned arena,
           so the input buffer has no lifetime requirements and the
           tree is fully self-contained — no shared mutable state. */
        ryml::Tree tree = ryml::parse_in_arena(
            ryml::csubstr(yaml_str, yaml_len));
        std::string json = ryml::emitrs_json<std::string>(tree);
        /* ryml doesn't escape control chars in string values;
           JSON requires U+0000..U+001F to be escaped. */
        std::string safe = escape_json_control_chars(json);
        return strdup_result(safe);
    } catch (const std::exception &e) {
        g_errmsg = e.what();
        return nullptr;
    }
}

} /* extern "C" */
