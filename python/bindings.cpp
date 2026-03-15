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
}
