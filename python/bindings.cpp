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

NB_MODULE(blobtemplates_ext, m) {
    m.doc() = "Inja/Jinja2 template rendering via the Inja C++ library";

    m.def("render", &py_render,
          nb::arg("template_str"), nb::arg("json_data"),
          "Render an Inja template against JSON data");

    m.def("render_with_options", &py_render_with_options,
          nb::arg("template_str"), nb::arg("json_data"), nb::arg("options"),
          "Render an Inja template with custom delimiter options");
}
