#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "cece/cece_scheme_runtime.hpp"
namespace py = pybind11;

void BindSchemeRuntime(py::module_& module) {
    py::class_<cece::SchemeFieldSpec>(module, "SchemeFieldSpec")
        .def_readonly("name", &cece::SchemeFieldSpec::name)
        .def_readonly("units", &cece::SchemeFieldSpec::units)
        .def_readonly("axis", &cece::SchemeFieldSpec::axis)
        .def_readonly("extent", &cece::SchemeFieldSpec::extent)
        .def_readonly("required", &cece::SchemeFieldSpec::required)
        .def_readonly("output", &cece::SchemeFieldSpec::output);
    py::class_<cece::SchemeDescriptor>(module, "SchemeDescriptor")
        .def_readonly("name", &cece::SchemeDescriptor::name)
        .def_readonly("fields", &cece::SchemeDescriptor::fields)
        .def_readonly("evolving_history", &cece::SchemeDescriptor::evolving_history);
    module.def(
        "describe_scheme", [](const std::string& name, const std::string& yaml, int levels) { return cece::DescribeScheme({name, yaml}, levels); },
        py::arg("name"), py::arg("options_yaml") = "{}", py::arg("nlev") = 1);
    py::class_<cece::SchemeRuntime>(module, "SchemeRuntime")
        .def(py::init([](const std::vector<std::pair<std::string, std::string>>& requests, int columns, int levels) {
                 std::vector<cece::SchemeRequest> schemes;
                 for (const auto& [name, yaml] : requests) schemes.push_back({name, yaml});
                 return std::make_unique<cece::SchemeRuntime>(schemes, columns, levels);
             }),
             py::arg("requests"), py::arg("ncol"), py::arg("nlev") = 1)
        .def("set_field",
             [](cece::SchemeRuntime& runtime, const std::string& name, py::array_t<double, py::array::f_style | py::array::forcecast> data) {
                 if (data.ndim() != 2 || data.shape(0) != runtime.ncol())
                     throw py::value_error("Canonical fields must have dimensions (column, axis)");
                 runtime.set_field(name, data.data(), data.size(), static_cast<int>(data.shape(1)));
             })
        .def("get_field",
             [](cece::SchemeRuntime& runtime, const std::string& name) {
                 auto values = runtime.get_field(name);
                 const auto columns = runtime.ncol();
                 const auto extent = static_cast<py::ssize_t>(values.size() / columns);
                 py::array_t<double> result({static_cast<py::ssize_t>(columns), extent});
                 auto out = result.mutable_unchecked<2>();
                 for (py::ssize_t k = 0; k < extent; ++k)
                     for (int i = 0; i < columns; ++i) out(i, k) = values[i + columns * k];
                 return result;
             })
        .def("step", &cece::SchemeRuntime::step, py::call_guard<py::gil_scoped_release>())
        .def("close", &cece::SchemeRuntime::close)
        .def("restore_time", &cece::SchemeRuntime::restore_time)
        .def_property_readonly("next_time", &cece::SchemeRuntime::next_time)
        .def_property_readonly("descriptors", &cece::SchemeRuntime::descriptors, py::return_value_policy::copy);
}
