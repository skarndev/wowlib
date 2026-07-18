// The Python extension: welder reflects namespace wowlib and lays the nanobind
// bindings down. Names are reshaped to PEP 8 (read_file stays read_file, factory
// spellings like `wotlk` are already pythonic); docstrings render Google-style;
// annotations come straight from the wowlib headers.
//
// Result<T> returns are unwrapped by the casters in result_casters.hpp: the
// success branch converts as T, the error branch raises wowlib.WowlibError with
// "<ErrorCode>: <message>".
#include <welder/vocabulary.hpp>

#include <nanobind/nanobind.h>
#include <nanobind/stl/filesystem.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/string_view.h>

#include <wowlib/wowlib.hpp>

#include "result_casters.hpp"

#include <welder/rods/python/nanobind/module.hpp>
#include <welder/rods/python/naming.hpp>

WELDER_MODULE(wowlib, nanobind,
              welder::welder<welder::rods::nanobind::rod<>,
                             welder::rods::python::pep8>)
{
  // wowlib.WowlibError <- wowlib::result_error (the Result casters' error branch)
  static nanobind::object exc_type =
    nanobind::exception<wowlib::result_error>(module, "WowlibError");
}