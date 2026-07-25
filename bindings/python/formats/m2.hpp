#pragma once

/** @file
    @brief Registration entry point for the M2 versioned-format facade.

    Mirrors src/wowlib/formats/m2/: for_version on every family base (M2,
    M2Root, Skin, M2ChunkedFile, Skeleton — the last three are era-subset families),
    the read/write/convert verbs on M2Base, the read/write verbs on
    SkeletonBase (skeletons are first-class shared entities), and the AnyX
    runtime union aliases. Called once from the module body after the welding
    walk. */

#include <nanobind/nanobind.h>

namespace wowlib_py::formats::m2
{
  namespace nb = nanobind;

  /** @brief Attach the M2 facade to the welded classes on @p module.
      @param module the root wowlib module (owns formats.m2 and its records). */
  void register_facade(nb::module_& module);
}
