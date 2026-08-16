#pragma once

/** @file
    @brief The per-format version-matrix weld registrations — one parallel TU
    per format (welds_<fmt>.cpp), called from the module body after the
    welder walk (which no longer sees the per-range aliases) and BEFORE the
    facades (which resolve the concrete welded classes by type). */

#include <nanobind/nanobind.h>

namespace wowlib_py::formats
{
  void register_wmo_welds(::nanobind::module_& root);
  void register_m2_welds(::nanobind::module_& root);
  void register_adt_welds(::nanobind::module_& root);
  void register_wdt_welds(::nanobind::module_& root);
  void register_wdl_welds(::nanobind::module_& root);
}
