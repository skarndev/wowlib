#pragma once

/** @file
    @brief The per-format version-matrix weld registrations — one parallel TU
    per format (welds_<fmt>.cpp), called from the module body after the
    welder walk (which no longer sees the per-range aliases) and BEFORE the
    facades (which resolve the concrete welded classes by type). */

#include <nanobind/nanobind.h>

namespace wowlib_py::formats
{
  void registerWmoWelds(::nanobind::module_& root);
  void registerM2Welds(::nanobind::module_& root);
  void registerAdtWelds(::nanobind::module_& root);
  void registerWdtWelds(::nanobind::module_& root);
  void registerWdlWelds(::nanobind::module_& root);
}
