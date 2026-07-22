#pragma once

/** @file
    @brief The WMO (world map object) versioned-format facade for Python.

    Mirrors @c src/wowlib/formats/wmo/ on the binding side. The per-version @c WMO,
    @c WMORoot, @c WMOGroup, ... classes and their chunk wire structs are welded
    automatically; this unit adds only the hand-written facade surface that cannot
    come from reflection:
    - @c for_version() on each family base (see @c facade.hpp), and
    - the assembly verbs @c read/@c write/@c convert on @c WMO, as native nanobind
      overloads so stubgen renders typed @c \@overload blocks. */

#include <nanobind/nanobind.h>

namespace wowlib_py::formats::wmo
{
  namespace nb = nanobind;

  /** @brief Attach the WMO facade to @p module.

      Wires @c for_version onto every WMO family base (WMO, WMORoot, WMOGroup,
      WMOGroupBody, and the WMOGroupHeader/WMOBatch wire-struct families) and the
      @c read/@c write/@c convert surface onto @c WMO. Call from the module body,
      after welder's walk has registered the welded classes.

      @param module the extension module being initialized. */
  void register_facade(nb::module_& module);
}
