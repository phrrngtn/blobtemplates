#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>

#include "blobtemplates.h"

namespace nb = nanobind;

static nb::str py_render(const std::string &tmpl, const std::string &json_data) {
    char *result = blobtemplates_render(tmpl.c_str(), json_data.c_str());
    if (!result) {
        throw nb::value_error(blobtemplates_errmsg());
    }
    nb::str out(result);
    blobtemplates_free(result);
    return out;
}

static nb::str py_render_with_options(const std::string &tmpl,
                                       const std::string &json_data,
                                       const std::string &options) {
    char *result = blobtemplates_render_with_options(tmpl.c_str(), json_data.c_str(),
                                                      options.c_str());
    if (!result) {
        throw nb::value_error(blobtemplates_errmsg());
    }
    nb::str out(result);
    blobtemplates_free(result);
    return out;
}

/* ── Helper for functions that return NULL on both error and null result ── */

static nb::object py_nullable_result(char *result) {
    if (result) {
        nb::str out(result);
        blobtemplates_free(result);
        return out;
    }
    const char *err = blobtemplates_errmsg();
    if (err && err[0]) {
        throw nb::value_error(err);
    }
    return nb::none();
}

static nb::object py_jmespath_search(const std::string &json,
                                      const std::string &expression) {
    return py_nullable_result(
        blobtemplates_jmespath_search(json.c_str(), expression.c_str()));
}

static nb::str py_json_from_diff(const std::string &source,
                                  const std::string &target) {
    char *result = blobtemplates_json_from_diff(source.c_str(), target.c_str());
    if (!result) throw nb::value_error(blobtemplates_errmsg());
    nb::str out(result);
    blobtemplates_free(result);
    return out;
}

static nb::str py_json_apply_patch(const std::string &json,
                                    const std::string &patch) {
    char *result = blobtemplates_json_apply_patch(json.c_str(), patch.c_str());
    if (!result) throw nb::value_error(blobtemplates_errmsg());
    nb::str out(result);
    blobtemplates_free(result);
    return out;
}

static nb::str py_json_diff(const std::string &source,
                             const std::string &target) {
    char *result = blobtemplates_json_diff(source.c_str(), target.c_str());
    if (!result) throw nb::value_error(blobtemplates_errmsg());
    nb::str out(result);
    blobtemplates_free(result);
    return out;
}

static nb::str py_json_patch(const std::string &source,
                              const std::string &patch) {
    char *result = blobtemplates_json_patch(source.c_str(), patch.c_str());
    if (!result) throw nb::value_error(blobtemplates_errmsg());
    nb::str out(result);
    blobtemplates_free(result);
    return out;
}

static nb::str py_json_flatten(const std::string &json) {
    char *result = blobtemplates_json_flatten(json.c_str());
    if (!result) throw nb::value_error(blobtemplates_errmsg());
    nb::str out(result);
    blobtemplates_free(result);
    return out;
}

static nb::str py_json_unflatten(const std::string &json) {
    char *result = blobtemplates_json_unflatten(json.c_str());
    if (!result) throw nb::value_error(blobtemplates_errmsg());
    nb::str out(result);
    blobtemplates_free(result);
    return out;
}

static nb::str py_yaml_to_json(const std::string &yaml) {
    char *result = blobtemplates_yaml_to_json(yaml.c_str());
    if (!result) throw nb::value_error(blobtemplates_errmsg());
    nb::str out(result);
    blobtemplates_free(result);
    return out;
}

static nb::str py_json_nest(const std::string &data_json,
                              const std::string &keys_json) {
    char *result = blobtemplates_json_nest(data_json.c_str(), keys_json.c_str());
    if (!result) throw nb::value_error(blobtemplates_errmsg());
    nb::str out(result);
    blobtemplates_free(result);
    return out;
}

static nb::str py_text_diff(const std::string &old_text,
                              const std::string &new_text,
                              nb::object label_old,
                              nb::object label_new) {
    std::string lo_s, ln_s;
    const char *lo = nullptr, *ln = nullptr;
    if (!label_old.is_none()) { lo_s = nb::cast<std::string>(label_old); lo = lo_s.c_str(); }
    if (!label_new.is_none()) { ln_s = nb::cast<std::string>(label_new); ln = ln_s.c_str(); }

    char *result = blobtemplates_text_diff(old_text.c_str(), new_text.c_str(), lo, ln, 3);
    if (!result) throw nb::value_error(blobtemplates_errmsg());
    nb::str out(result);
    blobtemplates_free(result);
    return out;
}

/* json_patch_fold: sequential fold in Python (no SQL aggregate needed) */
static nb::str py_json_patch_fold(nb::list values) {
    if (values.size() == 0) {
        throw nb::value_error("json_patch_fold requires at least one value");
    }

    /* Parse the first value as the base document */
    std::string first = nb::cast<std::string>(values[0]);
    blobtemplates_json_doc *doc = blobtemplates_json_doc_parse(first.c_str());
    if (!doc) throw nb::value_error(blobtemplates_errmsg());

    /* Apply subsequent values as patches */
    for (size_t i = 1; i < values.size(); i++) {
        if (values[i].is_none()) continue;
        std::string patch = nb::cast<std::string>(values[i]);
        if (blobtemplates_json_doc_apply_patch(doc, patch.c_str()) != 0) {
            blobtemplates_json_doc_destroy(doc);
            throw nb::value_error(blobtemplates_errmsg());
        }
    }

    char *json = blobtemplates_json_doc_serialize(doc);
    blobtemplates_json_doc_destroy(doc);
    if (!json) throw nb::value_error(blobtemplates_errmsg());
    nb::str out(json);
    blobtemplates_free(json);
    return out;
}

NB_MODULE(blobtemplates_ext, m) {
    m.doc() = "Inja/Jinja2 template rendering and JSON processing";

    m.def("render", &py_render,
          nb::arg("template_str"), nb::arg("json_data"),
          "Render an Inja template against JSON data");

    m.def("render_with_options", &py_render_with_options,
          nb::arg("template_str"), nb::arg("json_data"), nb::arg("options"),
          "Render an Inja template with custom delimiter options");

    m.def("jmespath_search", &py_jmespath_search,
          nb::arg("json"), nb::arg("expression"),
          "Search a JSON document with a JMESPath expression");

    m.def("json_from_diff", &py_json_from_diff,
          nb::arg("source"), nb::arg("target"),
          "Compute a JSON Patch (RFC 6902) from source to target (jsoncons)");

    m.def("json_apply_patch", &py_json_apply_patch,
          nb::arg("json"), nb::arg("patch"),
          "Apply a JSON Patch (RFC 6902) to a document (jsoncons)");

    m.def("json_diff", &py_json_diff,
          nb::arg("source"), nb::arg("target"),
          "Compute a JSON Patch (RFC 6902) from source to target (nlohmann)");

    m.def("json_patch", &py_json_patch,
          nb::arg("source"), nb::arg("patch"),
          "Apply a JSON Patch (RFC 6902) to a document (nlohmann)");

    m.def("json_flatten", &py_json_flatten,
          nb::arg("json"),
          "Flatten nested JSON to object with JSON Pointer keys");

    m.def("json_unflatten", &py_json_unflatten,
          nb::arg("json"),
          "Unflatten a JSON Pointer-keyed object back to nested JSON");

    m.def("yaml_to_json", &py_yaml_to_json,
          nb::arg("yaml_str"),
          "Convert a YAML string to JSON (via rapidyaml)");

    m.def("json_nest", &py_json_nest,
          nb::arg("data_json"), nb::arg("keys_json"),
          "Reshape a flat JSON array of objects into a nested hierarchy by key fields");

    m.def("text_diff", &py_text_diff,
          nb::arg("old_text"), nb::arg("new_text"),
          nb::arg("label_old") = nb::none(),
          nb::arg("label_new") = nb::none(),
          "Compute a unified diff between two text strings");

    m.def("json_patch_fold", &py_json_patch_fold,
          nb::arg("values"),
          "Fold a list of JSON values via RFC 6902 patch. "
          "First value is the base document; subsequent values are patches applied in order.");
}
