#include "blobtemplates.h"

#include <inja/inja.hpp>
#include <nlohmann/json.hpp>

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

} /* extern "C" */
